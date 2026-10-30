#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lwip/sockets.h"

namespace udp
{
class Server
{
public:
  Server() = default;
  ~Server();
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  esp_err_t initialize(std::uint16_t port);
  void close();
  // Timeout/interruption returns ESP_ERR_TIMEOUT; other receive errors close the socket.
  esp_err_t receive(void *data, std::size_t capacity, std::size_t &size, sockaddr_in &peer);
  esp_err_t reply(const sockaddr_in &peer, const void *data, std::size_t size) const;

private:
  int socket_ = -1;
};

class Client
{
public:
  Client() = default;
  ~Client();
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  // Open a socket for an IPv4 destination after the network stack is initialized.
  esp_err_t initialize(const char *host, uint16_t port);
  esp_err_t send(const void *data, size_t size) const;

private:
  int socket_ = -1;
  sockaddr_in destination_ = {};
};
} // namespace udp
