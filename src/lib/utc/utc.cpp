#include "utc.hpp"

#include <ctime>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "wifi.h"

namespace utc
{
esp_err_t initialize()
{
  wifi::wait_connected();
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_MONITORING_NTP_SERVER);
  const auto error = esp_netif_sntp_init(&config);
  if(error != ESP_OK)
    return error;
  ESP_LOGI("utc", "Waiting for Internet UTC synchronization");
  esp_err_t result;
  do
  {
    result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
  } while(result == ESP_ERR_TIMEOUT);
  if(result == ESP_OK)
    ESP_LOGI("utc", "UTC synchronized");
  return result;
}

std::uint32_t get()
{
  return static_cast<std::uint32_t>(std::time(nullptr));
}
} // namespace utc
