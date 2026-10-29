#include "monitor_rbamp_task.hpp"

#include <atomic>
#include "dex.hpp"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "monitoring.hpp"
#include "rbamp_async.hpp"
#include "sdkconfig.h"
#include "pikachu_control.hpp"
#include "udp_async.hpp"
#include "utc.hpp"
#include "wifi.h"

namespace
{
constexpr char TAG[] = "monitor_rbamp";
constexpr std::int64_t RetryUs = 5000000;
constexpr std::int64_t ReplayGapUs = 20000; // Bounded replay: at most 50 packets/s.
constexpr auto MonitoringCommand = static_cast<dex::CommandId>(dex::peripheral::PeripheralCommandId::Monitoring);
using Publication = dex::ResponsePacket<dex::peripheral::AddressedData<dex::Pikachu::MonitoringPublication>>;
static_assert(sizeof(Publication) == 64);
std::atomic<bool> sampleDue{false};
SemaphoreHandle_t controlMutex = nullptr;
pikachu::Control control{CONFIG_RBAMP_MONITOR_PERIPHERAL_ID, CONFIG_RBAMP_MONITOR_SAMPLE_PERIOD_MS, CONFIG_RBAMP_MONITOR_PUBLISH_PERIOD_MS};

bool IRAM_ATTR sample_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *)
{
  sampleDue.store(true, std::memory_order_release);
  return false;
}

struct Monitor
{
  rbamp_async::Meter meter;
  udp::AsyncClient collector;
  monitoring::History history;
  void *storage = nullptr;
  std::size_t historySlots = 0;
  dex::Pikachu::ConfigData config{};
  std::uint32_t configRevision = 0;
  gptimer_handle_t sampleTimer = nullptr;
  std::uint32_t inFlight = 0;
  bool sendPending = false, draining = false, profileError = false;
  std::int64_t nextNetwork = 0, nextPublish = 0, nextSend = 0, nextAllocation = 0;
  std::uint32_t capturedAt = 0, capturedUtc = 0;

  void configuration(std::int64_t now)
  {
    // A rare flash write must not block the acquisition state machine.
    if(xSemaphoreTake(controlMutex, 0) != pdTRUE)
      return;
    const auto revision = control.revision();
    const auto candidate = control.config();
    xSemaphoreGive(controlMutex);
    if(revision == configRevision)
      return;
    if(candidate.monitoring.samplePeriodMs != config.monitoring.samplePeriodMs)
    {
      gptimer_alarm_config_t alarm{};
      alarm.alarm_count = candidate.monitoring.samplePeriodMs * 1000ULL;
      alarm.flags.auto_reload_on_alarm = true;
      auto error = gptimer_set_alarm_action(sampleTimer, &alarm);
      if(error == ESP_OK)
        error = gptimer_set_raw_count(sampleTimer, 0);
      if(error != ESP_OK)
      {
        ESP_LOGE(TAG, "Cannot apply sample period: %s", esp_err_to_name(error));
        return;
      }
      nextAllocation = 0;
    }
    config = candidate;
    configRevision = revision;
    nextPublish = now + config.monitoring.publishPeriodMs * 1000LL;
    draining = false;
    sampleDue.store(false, std::memory_order_release);
  }

  bool compatible_profile()
  {
    const bool compatible = meter.basic_three_channel();
    if(!compatible && !profileError)
      ESP_LOGE(TAG, "The fixed BASIC format requires three current channels and one shared voltage input");
    profileError = !compatible;
    return compatible;
  }

  void network(std::int64_t now)
  {
    if(!wifi::is_connected())
      return;
    if(now >= nextNetwork)
    {
      if(!collector.ready() && CONFIG_RBAMP_MONITOR_HOST[0] != '\0')
        collector.initialize(CONFIG_RBAMP_MONITOR_HOST, CONFIG_RBAMP_MONITOR_PORT);
      nextNetwork = now + RetryUs;
    }
  }

  void allocate_history(std::int64_t now)
  {
    const auto slots = monitoring::History::capacity(config.monitoring.samplePeriodMs);
    if((storage != nullptr && historySlots >= slots) || !config.monitoring.enabled ||
       !meter.ready() || now < nextAllocation || !compatible_profile())
      return;
    const auto bytes = monitoring::History::required_bytes(config.monitoring.samplePeriodMs);
    void *replacement = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // Keep headroom for Wi-Fi/lwIP and I2C; do not consume their last RAM block.
    if(replacement == nullptr && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > bytes + 32768)
      replacement = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(replacement == nullptr)
    {
      ESP_LOGE(TAG, "One-hour BASIC FIFO needs %u bytes (%u ms); internal free/largest block: %u/%u bytes. No adequate RAM/PSRAM; acquisition is disabled.",
               static_cast<unsigned>(bytes), unsigned(config.monitoring.samplePeriodMs),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
               static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
      nextAllocation = now + 30000000;
      return;
    }
    if(storage == nullptr)
      history.initialize(replacement, slots);
    else
    {
      history.relocate(replacement, slots); // Growing only; preserves every entry.
      heap_caps_free(storage);
    }
    storage = replacement;
    historySlots = slots;
    nextPublish = now + config.monitoring.publishPeriodMs * 1000LL;
    ESP_LOGI(TAG, "One-hour FIFO: %u slots, %u bytes", unsigned(slots), unsigned(bytes));
  }

  void acquisition(std::int64_t now)
  {
    meter.poll(now);
    allocate_history(now);
    rbamp_async::Reading reading{};
    if(meter.take(reading) && storage != nullptr && config.monitoring.enabled)
    {
      dex::Pikachu::MonitoringPublication sample{};
      if(monitoring::make_basic_sample(reading, capturedUtc, sample))
        history.push(sample, capturedAt);
    }
    if(!sampleDue.exchange(false, std::memory_order_acquire) || !config.monitoring.enabled ||
       storage == nullptr || historySlots < monitoring::History::capacity(config.monitoring.samplePeriodMs) ||
        !meter.ready())
      return;
    if(!compatible_profile())
      return;
    const auto timestamp = utc::get();
    if(timestamp != 0 && meter.request(now))
    {
      capturedAt = static_cast<std::uint32_t>(now / 1000000);
      capturedUtc = timestamp;
    }
  }

  void publish(std::int64_t now)
  {
    if(storage == nullptr)
      return;
    history.expire(static_cast<std::uint32_t>(now / 1000000)); // Immune to UTC corrections.
    if(sendPending)
    {
      if(collector.send_pending())
        return;
      sendPending = false;
      // This is only local lwIP completion, never a receiver acknowledgement.
      if(collector.last_send_error() == ESP_OK)
        history.consume(inFlight);
      nextSend = now + ReplayGapUs;
    }
    if(!config.monitoring.enabled)
      return;
    if(now >= nextPublish)
    {
      draining = true;
      const std::int64_t period = config.monitoring.publishPeriodMs * 1000LL;
      nextPublish += ((now - nextPublish) / period + 1) * period;
    }
    if(history.size() == 0)
    {
      draining = false;
      return;
    }
    if(!draining || !collector.ready() || !wifi::is_connected() || now < nextSend)
      return;
    const auto id = history.front_id();
    Publication publication{};
    publication.command = MonitoringCommand;
    publication.data.peripheralId = CONFIG_RBAMP_MONITOR_PERIPHERAL_ID;
    publication.data.data = *history.front();
    if(collector.send(&publication, sizeof(publication)) == ESP_OK)
    {
      inFlight = id;
      sendPending = true;
    }
    else
      nextSend = now + ReplayGapUs;
  }
};

// Static objects retain stable ISR/TCPIP callback addresses and keep the FIFO
// out of the task stack. All mutable history/state belongs to this single task.
Monitor monitor;

void monitor_rbamp_task(void *)
{
  auto nextWake = xTaskGetTickCount();
  esp_err_t previousError = ESP_OK;
  while(true)
  {
    const auto now = esp_timer_get_time();
    monitor.configuration(now);
    monitor.network(now);
    monitor.acquisition(now);
    monitor.publish(now);
    if(monitor.meter.error() != previousError)
    {
      previousError = monitor.meter.error();
      ESP_LOGW(TAG, "I2C state: %s", esp_err_to_name(previousError));
    }
    // Periodic scheduler yield, never a wait for I/O or a semaphore. Only
    // bounded state-machine steps run on each tick; the ISR sets completion.
    xTaskDelayUntil(&nextWake, 1);
  }
}
} // namespace

dex::ProcessResult process_monitor_rbamp_request(const dex::Request &request, std::uint8_t *response, std::size_t capacity)
{
  config_store::Transaction transaction;
  if(!transaction)
  {
    const auto encoded = dex::encode_response({request.command, dex::Error::NotReady, {}}, response, capacity);
    return {encoded.error, encoded.size, dex::Action::None};
  }
  xSemaphoreTake(controlMutex, portMAX_DELAY);
  const auto result = control.process(request, response, capacity);
  xSemaphoreGive(controlMutex);
  return result;
}

esp_err_t restore_monitor_rbamp_config()
{
  controlMutex = xSemaphoreCreateMutex();
  if(controlMutex == nullptr)
    return ESP_ERR_NO_MEM;
  const auto result = control.restore();
  if(result != config_store::Result::Ok && result != config_store::Result::NotFound)
  {
    ESP_LOGE(TAG, "Cannot restore Pikachu configuration (%d)", int(result));
    return ESP_FAIL;
  }
  if(!pikachu::Control::valid(control.config()))
    return ESP_ERR_INVALID_ARG;
  monitor.config = control.config();
  monitor.configRevision = control.revision();
  return ESP_OK;
}

esp_err_t start_monitor_rbamp_task()
{
  if(CONFIG_RBAMP_MONITOR_SDA_GPIO == CONFIG_RBAMP_MONITOR_SCL_GPIO)
    return ESP_ERR_INVALID_ARG;
  auto error = monitor.meter.initialize({CONFIG_RBAMP_MONITOR_I2C_PORT, static_cast<gpio_num_t>(CONFIG_RBAMP_MONITOR_SDA_GPIO), static_cast<gpio_num_t>(CONFIG_RBAMP_MONITOR_SCL_GPIO), CONFIG_RBAMP_MONITOR_ADDRESS, CONFIG_RBAMP_MONITOR_CT_MODEL});
  if(error != ESP_OK)
    return error;
  gptimer_config_t timer{};
  timer.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  timer.direction = GPTIMER_COUNT_UP;
  timer.resolution_hz = 1000000;
  error = gptimer_new_timer(&timer, &monitor.sampleTimer);
  if(error != ESP_OK)
    return error;
  gptimer_event_callbacks_t callbacks{};
  callbacks.on_alarm = sample_alarm;
  error = gptimer_register_event_callbacks(monitor.sampleTimer, &callbacks, nullptr);
  if(error != ESP_OK)
    return error;
  gptimer_alarm_config_t alarm{};
  alarm.alarm_count = monitor.config.monitoring.samplePeriodMs * 1000ULL;
  alarm.flags.auto_reload_on_alarm = true;
  if((error = gptimer_set_alarm_action(monitor.sampleTimer, &alarm)) != ESP_OK ||
     (error = gptimer_enable(monitor.sampleTimer)) != ESP_OK ||
     (error = gptimer_start(monitor.sampleTimer)) != ESP_OK)
    return error;
  return xTaskCreate(monitor_rbamp_task, "monitor_rbamp", 6144, nullptr, 4, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
