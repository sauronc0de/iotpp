#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lwip/sockets.h"

namespace udp
{
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
