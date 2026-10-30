#include "server_dex_task.hpp"

#include "dex_server.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "udp.h"
#include "wifi.h"
#include "config_store.hpp"
#ifdef IOTPP_OTA
#include "ota_peripheral.hpp"
#ifdef IOTPP_MONITOR_DOOR
static_assert(CONFIG_OTA_PERIPHERAL_ID != CONFIG_DOOR_MONITOR_PERIPHERAL_ID,
              "DEX peripheral instance IDs must be unique");
#endif
#ifdef IOTPP_MONITOR_RBAMP
static_assert(CONFIG_OTA_PERIPHERAL_ID != CONFIG_RBAMP_MONITOR_PERIPHERAL_ID,
              "DEX peripheral instance IDs must be unique");
#endif
#endif
#ifdef IOTPP_MONITOR_DOOR
#include "monitor_door_task.hpp"
#endif
#ifdef IOTPP_MONITOR_RBAMP
#include "monitor_rbamp_task.hpp"
#endif
#if defined(IOTPP_MONITOR_DOOR) && defined(IOTPP_MONITOR_RBAMP)
static_assert(CONFIG_DOOR_MONITOR_PERIPHERAL_ID != CONFIG_RBAMP_MONITOR_PERIPHERAL_ID,
              "DEX peripheral instance IDs must be unique");
#endif

namespace
{
#ifdef IOTPP_OTA
const OtaPeripheral mew(CONFIG_OTA_PERIPHERAL_ID);

dex::ProcessResult device_info(const dex::Request &request, std::uint8_t *response, std::size_t capacity)
{
  // All typed peripheral InfoData layouts share the same six-byte representation.
  std::uint8_t payload[sizeof(dex::Version) + 3 * sizeof(dex::Mew::InfoData)]{};
  const dex::Version version{FIRMWARE_VERSION};
  std::memcpy(payload, &version, sizeof(version));
  std::size_t size = sizeof(version);
  const auto append = [&](std::uint8_t id, std::uint8_t type, dex::peripheral::PeripheralCapability capabilities) {
    dex::Mew::InfoData info{};
    info.id = id;
    info.version = {1, 0, 0};
    info.capabilities = capabilities;
    info.type = type;
    std::memcpy(payload + size, &info, sizeof(info));
    size += sizeof(info);
  };
  using Capability = dex::peripheral::PeripheralCapability;
#ifdef IOTPP_MONITOR_DOOR
  append(CONFIG_DOOR_MONITOR_PERIPHERAL_ID, 143, static_cast<Capability>(unsigned(Capability::Monitoring) | unsigned(Capability::Config)));
#endif
#ifdef IOTPP_MONITOR_RBAMP
  append(CONFIG_RBAMP_MONITOR_PERIPHERAL_ID, 25, static_cast<Capability>(unsigned(Capability::Monitoring) | unsigned(Capability::Config)));
#endif
  append(CONFIG_OTA_PERIPHERAL_ID, dex::Mew::Type, static_cast<Capability>(unsigned(Capability::Actions) | unsigned(Capability::Config)));
  const auto encoded = dex::encode_response({request.command, dex::Error::None, {payload, size}}, response, capacity);
  return {encoded.error, encoded.size, dex::Action::None};
}
#endif

dex::ProcessResult peripheral_request(const dex::Request &request, std::uint8_t *response, std::size_t capacity)
{
  if(request.data.data != nullptr && request.data.size != 0)
  {
#ifdef IOTPP_OTA
    if(request.data.data[0] == CONFIG_OTA_PERIPHERAL_ID)
      return mew.process(request, response, capacity);
#endif
#ifdef IOTPP_MONITOR_DOOR
    if(request.data.data[0] == CONFIG_DOOR_MONITOR_PERIPHERAL_ID)
      return process_monitor_door_request(request, response, capacity);
#endif
#ifdef IOTPP_MONITOR_RBAMP
    if(request.data.data[0] == CONFIG_RBAMP_MONITOR_PERIPHERAL_ID)
      return process_monitor_rbamp_request(request, response, capacity);
#endif
  }
#if defined(IOTPP_MONITOR_DOOR) || defined(IOTPP_MONITOR_RBAMP) || defined(IOTPP_OTA)
  constexpr auto error = dex::Error::InvalidParameter;
#else
  constexpr auto error = dex::Error::UnsupportedOperation;
#endif
  const auto encoded = dex::encode_response({request.command, error, {}}, response, capacity);
  return {encoded.error, encoded.size, dex::Action::None};
}

void server_dex_task(void *)
{
#ifdef IOTPP_OTA
  const dex::Server server(dex::Version{FIRMWARE_VERSION}, peripheral_request, device_info);
#else
  const dex::Server server(dex::Version{FIRMWARE_VERSION}, peripheral_request);
#endif
  udp::Server transport;
  // Keep one extra byte so truncated oversized datagrams are still rejected.
  // Only this task uses the buffers; static storage keeps them off the task stack.
  static std::uint8_t request[dex::MaxDatagramSize + 1];
  static std::uint8_t response[dex::MaxDatagramSize];

  while(true)
  {
    wifi::wait_connected();
    if(transport.initialize(CONFIG_SENTINEL_UDP_PORT) != ESP_OK)
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

      // Hold off chip-wide sleep through persistence, application and the reply.
      config_store::Transaction transaction;
      dex::ProcessResult result;
      if(transaction)
        result = server.process({request, size}, response, sizeof(response));
      else if(size != 0)
      {
        const auto encoded = dex::encode_response({static_cast<dex::CommandId>(request[0]), dex::Error::NotReady, {}}, response, sizeof(response));
        result = {encoded.error, encoded.size, dex::Action::None};
      }
      if(result.error != dex::Error::None)
      {
        ESP_LOGE("sentinel", "Cannot encode DEX reply: 0x%08lx", static_cast<unsigned long>(result.error));
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
} // namespace

esp_err_t start_server_dex_task()
{
  const auto result = xTaskCreate(
      server_dex_task, "server_dex", 4096, nullptr, 5, nullptr);
  return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
