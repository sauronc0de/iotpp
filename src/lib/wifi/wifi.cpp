#include "wifi.h"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

namespace
{
constexpr char TAG[] = "wifi";
constexpr EventBits_t CONNECTED_BIT = BIT0;
EventGroupHandle_t events;

void event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    ESP_ERROR_CHECK(esp_wifi_connect());
  }
  else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    xEventGroupClearBits(events, CONNECTED_BIT);
    ESP_LOGW(TAG, "WiFi disconnected; reconnecting...");
    ESP_ERROR_CHECK(esp_wifi_connect());
  }
  else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    const auto *event = static_cast<const ip_event_got_ip_t *>(event_data);
    ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(events, CONNECTED_BIT);
  }
}
} // namespace

namespace wifi
{
esp_err_t initialize(const char *ssid, const char *password)
{
  wifi_config_t config = {};
  if(ssid == nullptr || ssid[0] == '\0')
  {
    ESP_LOGE(TAG, "WiFi SSID is required");
    return ESP_ERR_INVALID_ARG;
  }
  if(password == nullptr || std::strlen(ssid) > sizeof(config.sta.ssid) ||
     std::strlen(password) > sizeof(config.sta.password))
  {
    ESP_LOGE(TAG, "Invalid WiFi credentials: check SSID and password lengths");
    return ESP_ERR_INVALID_ARG;
  }
  if(events != nullptr)
  {
    ESP_LOGE(TAG, "WiFi is already initialized");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t result = nvs_flash_init();
  if(result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(result);

  events = xEventGroupCreate();
  ESP_ERROR_CHECK(events != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, nullptr));

  std::memcpy(config.sta.ssid, ssid, std::strlen(ssid));
  std::memcpy(config.sta.password, password, std::strlen(password));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
  ESP_ERROR_CHECK(esp_wifi_start());
  return ESP_OK;
}

void wait_connected()
{
  xEventGroupWaitBits(events, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

bool is_connected()
{
  return events != nullptr && (xEventGroupGetBits(events) & CONNECTED_BIT) != 0;
}
} // namespace wifi
