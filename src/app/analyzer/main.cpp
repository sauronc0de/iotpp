#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "udp.h"
#include "wifi.h"

namespace
{
constexpr char TAG[] = "analyzer";
} // namespace

extern "C" void app_main()
{
  const esp_err_t wifi_result = wifi::initialize(CONFIG_ANALYZER_WIFI_SSID, CONFIG_ANALYZER_WIFI_PASSWORD);
  if(wifi_result != ESP_OK)
  {
    ESP_LOGE(TAG, "WiFi initialization failed: %s (0x%x)", esp_err_to_name(wifi_result), static_cast<unsigned int>(wifi_result));
    return;
  }

  udp::Client client;
  const esp_err_t udp_result = client.initialize(CONFIG_ANALYZER_UDP_HOST, CONFIG_ANALYZER_UDP_PORT);
  if(udp_result != ESP_OK)
  {
    ESP_LOGE(TAG, "UDP initialization failed: %s (0x%x)", esp_err_to_name(udp_result), static_cast<unsigned int>(udp_result));
    return;
  }
  constexpr char message[] = "Hello World from iotpp analyzer!\n";
  while(true)
  {
    wifi::wait_connected();
    const esp_err_t send_result = client.send(message, sizeof(message) - 1);
    if(send_result != ESP_OK)
    {
      ESP_LOGE(TAG, "UDP send failed: %s (0x%x)", esp_err_to_name(send_result), static_cast<unsigned int>(send_result));
    }
    else
    {
      ESP_LOGI(TAG, "Sent Hello World to %s:%d", CONFIG_ANALYZER_UDP_HOST, CONFIG_ANALYZER_UDP_PORT);
    }
    vTaskDelay(pdMS_TO_TICKS(CONFIG_ANALYZER_UDP_INTERVAL_MS));
  }
}
