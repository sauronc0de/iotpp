#include "udp.h"

#include <cerrno>

#include "esp_log.h"

namespace
{
constexpr char TAG[] = "udp";
}

namespace udp
{
Server::~Server()
{
  close();
}

void Server::close()
{
  if(socket_ >= 0)
  {
    ::close(socket_);
    socket_ = -1;
  }
}

esp_err_t Server::initialize(std::uint16_t port)
{
  if(socket_ >= 0)
    return ESP_ERR_INVALID_STATE;
  if(port == 0)
    return ESP_ERR_INVALID_ARG;

  socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(socket_ < 0)
  {
    ESP_LOGE(TAG, "Cannot create UDP socket: errno %d", errno);
    return ESP_FAIL;
  }

  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(port);
  const timeval timeout{1, 0};
  if(setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
     bind(socket_, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) != 0)
  {
    ESP_LOGE(TAG, "Cannot bind UDP listener: errno %d", errno);
    close();
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Listening on UDP port %u", static_cast<unsigned int>(port));
  return ESP_OK;
}

esp_err_t Server::receive(void *data, std::size_t capacity, std::size_t &size, sockaddr_in &peer)
{
  size = 0;
  if(socket_ < 0)
    return ESP_ERR_INVALID_STATE;
  if(data == nullptr || capacity == 0)
    return ESP_ERR_INVALID_ARG;

  socklen_t peerSize = sizeof(peer);
  const auto received = recvfrom(socket_, data, capacity, 0,
                                 reinterpret_cast<sockaddr *>(&peer), &peerSize);
  if(received < 0)
  {
    if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return ESP_ERR_TIMEOUT;
    ESP_LOGE(TAG, "UDP receive failed: errno %d", errno);
    close();
    return ESP_FAIL;
  }
  size = static_cast<std::size_t>(received);
  return ESP_OK;
}

esp_err_t Server::reply(const sockaddr_in &peer, const void *data, std::size_t size) const
{
  if(socket_ < 0)
    return ESP_ERR_INVALID_STATE;
  const auto sent = sendto(socket_, data, size, 0,
                           reinterpret_cast<const sockaddr *>(&peer), sizeof(peer));
  if(sent != static_cast<ssize_t>(size))
  {
    ESP_LOGE(TAG, "UDP reply failed: errno %d", errno);
    return ESP_FAIL;
  }
  return ESP_OK;
}

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
