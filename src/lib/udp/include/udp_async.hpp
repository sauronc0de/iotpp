#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

namespace udp
{
// Raw lwIP callbacks run on the existing TCP/IP task. No socket waits, DNS
// waits, semaphore waits or extra application task. Must outlive the network
// stack: callbacks retain this object's address (use static storage).
class AsyncClient
{
public:
  static constexpr std::size_t Capacity = 1200;
  AsyncClient() = default;
  AsyncClient(const AsyncClient &) = delete;
  AsyncClient &operator=(const AsyncClient &) = delete;
  esp_err_t initialize(const char *host, std::uint16_t port);
  // Use a fresh source port for the next exchange so a delayed header-only
  // reply from a previous exchange cannot confirm a different message.
  esp_err_t new_exchange();
  bool ready() const { return state_.load(std::memory_order_acquire) == State::Ready; }
  esp_err_t send(const void *data, std::size_t size);
  esp_err_t receive(void *data, std::size_t capacity, std::size_t &size);
  esp_err_t last_send_error() const { return sendError_.load(std::memory_order_acquire); }

private:
  enum class State
  {
    Closed,
    Opening,
    Ready,
    Failed
  };
  enum class Rx
  {
    Empty,
    Writing,
    Ready
  };
  static void open_callback(void *context);
  static void exchange_callback(void *context);
  static void dns_callback(const char *, const ip_addr_t *address, void *context);
  static void send_callback(void *context);
  static void receive_callback(void *context, udp_pcb *, pbuf *packet, const ip_addr_t *, u16_t);
  void connect(const ip_addr_t *address);

  std::atomic<State> state_{State::Closed};
  std::atomic<bool> sending_{false};
  std::atomic<Rx> received_{Rx::Empty};
  std::atomic<esp_err_t> sendError_{ESP_OK};
  udp_pcb *pcb_ = nullptr; // Accessed only from the TCP/IP task.
  ip_addr_t peer_{};
  std::array<char, 128> host_{};
  std::uint16_t port_ = 0;
  std::array<std::uint8_t, Capacity> tx_{};
  std::array<std::uint8_t, Capacity> rx_{};
  std::size_t txSize_ = 0;
  std::size_t rxSize_ = 0;
};
} // namespace udp
