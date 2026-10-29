#include "monitor_door_task.hpp"

#include <array>
#include "dex.hpp"
#include "door_state.hpp"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "snorlax_control.hpp"
#include "udp_async.hpp"
#include "wifi.h"

namespace
{
constexpr char TAG[] = "monitor_door";
constexpr auto Pin = static_cast<gpio_num_t>(CONFIG_DOOR_MONITOR_GPIO);
#ifdef CONFIG_DOOR_MONITOR_INTERNAL_PULL
constexpr bool InternalPull = true;
#else
constexpr bool InternalPull = false;
#endif
constexpr std::int64_t RetryUs = 1000000;
using Publication = dex::ResponsePacket<dex::peripheral::AddressedData<dex::Snorlax::MonitoringPublication>>;
static_assert(sizeof(Publication) == 15);
SemaphoreHandle_t controlMutex = nullptr;
door::Control control{CONFIG_DOOR_MONITOR_PERIPHERAL_ID, CONFIG_DOOR_MONITOR_SAMPLE_PERIOD_MS};

struct Monitor
{
  door::State state{CONFIG_DOOR_MONITOR_CLOSED_READINGS};
  udp::AsyncClient collector;
  // Bounded outage buffer. Keep the in-flight front stable on overflow.
  std::array<dex::Snorlax::MonitoringPublication, 32> events{};
  unsigned head = 0, count = 0;
  std::uint32_t session = 0, sequence = 0, configRevision = 0;
  bool sending = false, enabled = true;
  std::int64_t nextNetwork = 0, nextSend = 0, sleepAfter = 0;
#if CONFIG_PM_ENABLE
  esp_pm_lock_handle_t awakeLock = nullptr;
#endif

  bool open() const { return gpio_get_level(Pin) == CONFIG_DOOR_MONITOR_OPEN_LEVEL; }

  void observe(bool isOpen)
  {
    const auto change = state.observe(isOpen);
    if(change == door::State::Change::None)
      return;
    sleepAfter = 0;
    const auto value = change == door::State::Change::Open ? dex::Snorlax::State::Open : dex::Snorlax::State::Closed;
    xSemaphoreTake(controlMutex, portMAX_DELAY);
    control.observe(value);
    xSemaphoreGive(controlMutex);
    if(!enabled)
      return;
    dex::Snorlax::MonitoringPublication event{session, sequence++, value};
    if(count == events.size())
    {
      // Preserve the older events and the most recent state; sequence gaps
      // expose lost transitions to the receiver during prolonged outages.
      events[(head + count - 1) % events.size()] = event;
      ESP_LOGW(TAG, "Event buffer full; replacing newest queued transition");
    }
    else
      events[(head + count++) % events.size()] = event;
    ESP_LOGI(TAG, "Door %s", isOpen ? "open" : "closed (confirmed)");
  }

  void publish(std::int64_t now)
  {
    if(sending)
    {
      if(collector.send_pending())
        return;
      sending = false;
      if(collector.last_send_error() == ESP_OK)
      {
        head = (head + 1) % events.size();
        --count;
        if(count == 0 && state.closed())
          sleepAfter = now + CONFIG_DOOR_MONITOR_TX_GRACE_MS * 1000LL;
      }
      else
        nextSend = now + RetryUs;
    }
    if(!enabled)
    {
      count = 0;
      return;
    }
    if(!wifi::is_connected())
      return;
    if(!collector.ready() && now >= nextNetwork)
    {
      collector.initialize(CONFIG_DOOR_MONITOR_HOST, CONFIG_DOOR_MONITOR_PORT);
      nextNetwork = now + RetryUs;
    }
    if(count == 0 || !collector.ready() || now < nextSend)
      return;
    Publication packet{};
    packet.command = static_cast<dex::CommandId>(dex::peripheral::PeripheralCommandId::Monitoring);
    packet.data.peripheralId = CONFIG_DOOR_MONITOR_PERIPHERAL_ID;
    packet.data.data = events[head];
    if(collector.send(&packet, sizeof(packet)) == ESP_OK)
      sending = true;
    else
      nextSend = now + RetryUs;
  }

  void sleep(std::int64_t now)
  {
    if(!enabled || !state.closed() || count != 0 || sending || sleepAfter == 0 || now < sleepAfter)
      return;
    // Recheck after the TX grace period. EXT0 is level-triggered, so an
    // opening between this check and sleep immediately wakes the ESP32.
    if(open())
    {
      observe(true);
      return;
    }
    xSemaphoreTake(controlMutex, portMAX_DELAY);
    const bool permitted = control.can_sleep() && config_store::try_begin_sleep() && control.begin_sleep();
    xSemaphoreGive(controlMutex);
    if(!permitted)
      return;
    ESP_LOGI(TAG, "Entering Deep Sleep; GPIO %d wakes at level %d", int(Pin), CONFIG_DOOR_MONITOR_OPEN_LEVEL);
    esp_deep_sleep_start();
  }
};

// Stable storage is required by AsyncClient's TCP/IP callbacks.
Monitor monitor;

void monitor_door_task(void *)
{
  // Capture the wake event even if the door closed during firmware/Wi-Fi boot.
  if(esp_sleep_get_wakeup_causes() & (1UL << ESP_SLEEP_WAKEUP_EXT0))
    monitor.observe(true);
  const auto ticks = pdMS_TO_TICKS(CONFIG_DOOR_MONITOR_SAMPLE_PERIOD_MS);
  while(true)
  {
    const auto now = esp_timer_get_time();
    xSemaphoreTake(controlMutex, portMAX_DELAY);
    const bool enabled = control.enabled();
    const auto revision = control.revision();
    xSemaphoreGive(controlMutex);
    if(revision != monitor.configRevision)
    {
      monitor.configRevision = revision;
      monitor.enabled = enabled;
      monitor.sleepAfter = 0;
      monitor.count = monitor.sending ? 1 : 0;
      if(enabled)
        monitor.state = door::State{CONFIG_DOOR_MONITOR_CLOSED_READINGS};
    }
    monitor.observe(monitor.open());
    monitor.publish(now);
    monitor.sleep(now);
    // Do not catch up missed periods with consecutive immediate readings:
    // closure confirmation requires genuinely spaced samples.
    vTaskDelay(ticks > 0 ? ticks : 1);
  }
}
} // namespace

dex::ProcessResult process_monitor_door_request(const dex::Request &request, std::uint8_t *response, std::size_t capacity)
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

esp_err_t restore_monitor_door_config()
{
  controlMutex = xSemaphoreCreateMutex();
  if(controlMutex == nullptr)
    return ESP_ERR_NO_MEM;
  const auto result = control.restore();
  if(result != config_store::Result::Ok && result != config_store::Result::NotFound)
  {
    ESP_LOGE(TAG, "Cannot restore Snorlax configuration (%d)", int(result));
    return ESP_FAIL;
  }
  monitor.enabled = control.enabled();
  monitor.configRevision = control.revision();
  return ESP_OK;
}

esp_err_t start_monitor_door_task()
{
  if(!GPIO_IS_VALID_GPIO(Pin) || !rtc_gpio_is_valid_gpio(Pin) || CONFIG_DOOR_MONITOR_HOST[0] == '\0')
  {
    ESP_LOGE(TAG, "An RTC-capable input GPIO and a UDP receiver are required");
    return ESP_ERR_INVALID_ARG;
  }
  // GPIO34-39 on classic ESP32 have no internal pull resistors.
  if(InternalPull && !GPIO_IS_VALID_OUTPUT_GPIO(Pin))
  {
    ESP_LOGE(TAG, "GPIO %d requires an external pull resistor", int(Pin));
    return ESP_ERR_INVALID_ARG;
  }
  // EXT0 wake leaves the pad in RTC mode. Restore digital input on every boot.
  esp_err_t error = rtc_gpio_deinit(Pin);
  if(error != ESP_OK)
    return error;
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << Pin;
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = InternalPull && CONFIG_DOOR_MONITOR_OPEN_LEVEL == 1 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  config.pull_down_en = InternalPull && CONFIG_DOOR_MONITOR_OPEN_LEVEL == 0 ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  if((error = gpio_config(&config)) != ESP_OK)
    return error;
  if((error = rtc_gpio_pullup_dis(Pin)) != ESP_OK || (error = rtc_gpio_pulldown_dis(Pin)) != ESP_OK)
    return error;
  if(InternalPull)
  {
    error = CONFIG_DOOR_MONITOR_OPEN_LEVEL == 1 ? rtc_gpio_pullup_en(Pin) : rtc_gpio_pulldown_en(Pin);
    if(error != ESP_OK)
      return error;
  }
  if((error = esp_sleep_enable_ext0_wakeup(Pin, CONFIG_DOOR_MONITOR_OPEN_LEVEL)) != ESP_OK ||
     (error = esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)) != ESP_OK)
    return error;
#if CONFIG_PM_ENABLE
  if((error = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "door_open", &monitor.awakeLock)) != ESP_OK ||
     (error = esp_pm_lock_acquire(monitor.awakeLock)) != ESP_OK)
    return error;
#endif
  monitor.session = esp_random();
  return xTaskCreate(monitor_door_task, "monitor_door", 4096, nullptr, 4, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
