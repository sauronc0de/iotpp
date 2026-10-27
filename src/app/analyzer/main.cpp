#include "dex_server.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "udp.h"
#include "wifi.h"

extern "C" void app_main()
{
  ESP_ERROR_CHECK(wifi::initialize(CONFIG_ANALYZER_WIFI_SSID, CONFIG_ANALYZER_WIFI_PASSWORD));
  const dex::Server server(dex::Version{FIRMWARE_VERSION});
  udp::Server transport;
  // Keep one extra byte so truncated oversized datagrams are still rejected.
  // Only this task uses the buffers; static storage keeps them off the task stack.
  static std::uint8_t request[dex::MaxDatagramSize + 1];
  static std::uint8_t response[dex::MaxDatagramSize];

  while(true)
  {
    wifi::wait_connected();
    if(transport.initialize(CONFIG_ANALYZER_UDP_PORT) != ESP_OK)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    while(true)
    {
      wifi::wait_connected();
      sockaddr_in peer{};
      std::size_t size = 0;
      const auto received = transport.receive(request, sizeof(request), size, peer);
      if(received == ESP_ERR_TIMEOUT)
        continue;
      if(received != ESP_OK)
        break;

      const auto result = server.process({request, size}, response, sizeof(response));
      if(result.error != dex::Error::None)
      {
        ESP_LOGE("analyzer", "Cannot encode DEX reply: 0x%08lx", static_cast<unsigned long>(result.error));
        continue;
      }
      if(result.responseSize == 0)
        continue;

      if(transport.reply(peer, response, result.responseSize) == ESP_OK && result.action == dex::Action::Restart)
      {
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
      }
    }
    transport.close();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
