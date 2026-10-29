#include "monitor_rbamp_task.hpp"

#include <array>
#include <atomic>
#include "dex.hpp"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitoring.hpp"
#include "rbamp_async.hpp"
#include "sdkconfig.h"
#include "udp_async.hpp"
#include "wifi.h"

namespace
{
constexpr char TAG[] = "monitor_rbamp";
constexpr std::int64_t RetryUs = 5000000;
constexpr std::int64_t AckTimeoutUs = 2000000;
constexpr std::int64_t ReplayGapUs = 20000; // Bounded replay: at most 50 packets/s.
constexpr auto MonitoringCommand = static_cast<dex::CommandId>(dex::peripheral::PeripheralCommandId::Monitoring);
using Publication = dex::ResponsePacket<dex::peripheral::AddressedData<dex::Pikachu::MonitoringPublication>>;
static_assert(sizeof(Publication) == 72);
std::atomic<bool> sampleDue{false};

bool IRAM_ATTR sample_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *)
{
  sampleDue.store(true, std::memory_order_release);
  return false;
}

struct Monitor
{
  rbamp_async::Meter meter;
  udp::AsyncClient collector, ntp;
  monitoring::History history;
  monitoring::UtcClock clock;
  std::uint32_t session = 0;
  void *storage = nullptr;
  gptimer_handle_t sampleTimer = nullptr;
  monitoring::SampleId inFlight{}, exchangeFor{};
  bool awaitingResult = false, exchangePrepared = false, draining = false, profileError = false;
  std::int64_t nextNtp = 0, nextNetwork = 0, nextPublish = 0, nextSend = 0, nextAllocation = 0;
  std::uint32_t capturedAt = 0, capturedUtc = 0;
  std::array<std::uint8_t, dex::MaxDatagramSize> received{};

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
      if(!ntp.ready())
        ntp.initialize(CONFIG_RBAMP_MONITOR_NTP_SERVER, 123);
      if(!collector.ready() && CONFIG_RBAMP_MONITOR_HOST[0] != '\0')
        collector.initialize(CONFIG_RBAMP_MONITOR_HOST, CONFIG_RBAMP_MONITOR_PORT);
      nextNetwork = now + RetryUs;
    }
    std::size_t size = 0;
    if(ntp.receive(received.data(), received.size(), size) == ESP_OK && clock.accept({received.data(), size}, now))
    {
      nextNtp = now + monitoring::HourUs;
      ESP_LOGI(TAG, "UTC synchronized: %lu", static_cast<unsigned long>(clock.seconds(now)));
    }
    if(now >= nextNtp && ntp.ready())
    {
      const auto request = clock.request((std::uint64_t(esp_random()) << 32) | esp_random(), now);
      ntp.send(request.data(), request.size());
      nextNtp = now + RetryUs;
    }
    if(collector.receive(received.data(), received.size(), size) == ESP_OK)
    {
      dex::Response response;
      if(size == dex::ResponseHeaderSize && awaitingResult &&
         dex::decode_response({received.data(), size}, response) == dex::Error::None &&
         response.command == MonitoringCommand && response.result == dex::Error::None)
      {
        history.confirm(inFlight);
        awaitingResult = false;
        nextSend = now + ReplayGapUs;
      }
    }
  }

  void allocate_history(std::int64_t now)
  {
    if(storage != nullptr || !meter.ready() || now < nextAllocation || !compatible_profile())
      return;
    const auto bytes = monitoring::History::required_bytes(CONFIG_RBAMP_MONITOR_SAMPLE_PERIOD_MS);
    storage = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // Keep headroom for Wi-Fi/lwIP and I2C; do not consume their last RAM block.
    if(storage == nullptr && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > bytes + 32768)
      storage = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(storage == nullptr)
    {
      ESP_LOGE(TAG, "One-hour BASIC FIFO needs %u bytes (%u ms); internal free/largest block: %u/%u bytes. No adequate RAM/PSRAM; acquisition is disabled.",
               static_cast<unsigned>(bytes), CONFIG_RBAMP_MONITOR_SAMPLE_PERIOD_MS,
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
               static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
      nextAllocation = now + 30000000;
      return;
    }
    history.initialize(storage, monitoring::History::capacity(CONFIG_RBAMP_MONITOR_SAMPLE_PERIOD_MS), session);
    ESP_LOGI(TAG, "One-hour FIFO: %u slots, %u bytes", static_cast<unsigned>(monitoring::History::capacity(CONFIG_RBAMP_MONITOR_SAMPLE_PERIOD_MS)), static_cast<unsigned>(bytes));
  }

  void acquisition(std::int64_t now)
  {
    meter.poll(now);
    allocate_history(now);
    rbamp_async::Reading reading{};
    if(meter.take(reading) && storage != nullptr)
    {
      dex::Pikachu::BasicSample sample{};
      if(monitoring::make_basic_sample(reading, capturedUtc, sample))
        history.push(sample, capturedAt);
    }
    if(!sampleDue.exchange(false, std::memory_order_acquire) || storage == nullptr || !clock.valid() || !meter.ready())
      return;
    if(!compatible_profile())
      return;
    const auto utc = clock.seconds(now);
    if(utc != 0 && meter.request(now))
    {
      capturedAt = static_cast<std::uint32_t>(now / 1000000);
      capturedUtc = utc;
    }
  }

  void publish(std::int64_t now)
  {
    if(storage == nullptr)
      return;
    history.expire(static_cast<std::uint32_t>(now / 1000000)); // Immune to UTC corrections.
    if(now >= nextPublish)
    {
      draining = true;
      nextPublish = now + CONFIG_RBAMP_MONITOR_PUBLISH_PERIOD_MS * 1000LL;
    }
    if(history.size() == 0)
    {
      draining = false;
      awaitingResult = false;
      exchangePrepared = false;
      return;
    }
    if(!draining || !collector.ready() || !wifi::is_connected() || now < nextSend)
      return;
    const auto id = history.front_id();
    if(!exchangePrepared || exchangeFor.session != id.session || exchangeFor.sequence != id.sequence)
    {
      if(collector.new_exchange() == ESP_OK)
      {
        exchangeFor = id;
        exchangePrepared = true;
        awaitingResult = false;
      }
      return;
    }
    awaitingResult = false;
    Publication publication{};
    publication.command = MonitoringCommand;
    publication.data.peripheralId = CONFIG_RBAMP_MONITOR_PERIPHERAL_ID;
    publication.data.data = {id.session, id.sequence, *history.front()};
    if(collector.send(&publication, sizeof(publication)) == ESP_OK)
    {
      inFlight = id;
      awaitingResult = true;
      nextSend = now + AckTimeoutUs;
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

esp_err_t start_monitor_rbamp_task()
{
  if(CONFIG_RBAMP_MONITOR_SDA_GPIO == CONFIG_RBAMP_MONITOR_SCL_GPIO ||
     CONFIG_RBAMP_MONITOR_PUBLISH_PERIOD_MS < CONFIG_RBAMP_MONITOR_SAMPLE_PERIOD_MS)
    return ESP_ERR_INVALID_ARG;
  monitor.session = esp_random();
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
  alarm.alarm_count = CONFIG_RBAMP_MONITOR_SAMPLE_PERIOD_MS * 1000ULL;
  alarm.flags.auto_reload_on_alarm = true;
  if((error = gptimer_set_alarm_action(monitor.sampleTimer, &alarm)) != ESP_OK ||
     (error = gptimer_enable(monitor.sampleTimer)) != ESP_OK ||
     (error = gptimer_start(monitor.sampleTimer)) != ESP_OK)
    return error;
  return xTaskCreate(monitor_rbamp_task, "monitor_rbamp", 6144, nullptr, 4, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
