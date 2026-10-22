#include "udp.h"

#include <cerrno>

#include "esp_log.h"

namespace
{
constexpr char TAG[] = "udp";
}

namespace udp
{
Client::~Client()
{
  if(socket_ >= 0)
  {
    close(socket_);
  }
}

esp_err_t Client::initialize(const char *host, uint16_t port)
{
  if(socket_ >= 0)
  {
    return ESP_ERR_INVALID_STATE;
  }
  destination_ = {};
  destination_.sin_family = AF_INET;
  destination_.sin_port = htons(port);
  if(host == nullptr || port == 0 || inet_pton(AF_INET, host, &destination_.sin_addr) != 1)
  {
    ESP_LOGE(TAG, "Invalid destination IPv4 address or port");
    return ESP_ERR_INVALID_ARG;
  }

  socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(socket_ < 0)
  {
    ESP_LOGE(TAG, "Cannot create UDP socket: errno %d", errno);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t Client::send(const void *data, size_t size) const
{
  if(socket_ < 0)
  {
    return ESP_ERR_INVALID_STATE;
  }
  const int sent = sendto(socket_, data, size, 0,
                          reinterpret_cast<const sockaddr *>(&destination_), sizeof(destination_));
  if(sent < 0)
  {
    ESP_LOGE(TAG, "UDP send failed: errno %d", errno);
    return ESP_FAIL;
  }
  return ESP_OK;
}
} // namespace udp
