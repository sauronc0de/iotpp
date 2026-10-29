#include "udp_async.hpp"

#include <cstring>
#include "lwip/dns.h"
#include "lwip/tcpip.h"

namespace udp
{
esp_err_t AsyncClient::initialize(const char *host, std::uint16_t port)
{
  if(host == nullptr || *host == '\0' || std::strlen(host) >= host_.size() || port == 0)
    return ESP_ERR_INVALID_ARG;
  auto state = state_.load(std::memory_order_acquire);
  if(state == State::Opening || state == State::Ready)
    return ESP_ERR_INVALID_STATE;
  std::strcpy(host_.data(), host);
  port_ = port;
  state_.store(State::Opening, std::memory_order_release);
  if(tcpip_try_callback(open_callback, this) != ERR_OK)
  {
    state_.store(State::Failed, std::memory_order_release);
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

void AsyncClient::open_callback(void *context)
{
  auto &self = *static_cast<AsyncClient *>(context);
  ip_addr_t address{};
  const auto error = dns_gethostbyname(self.host_.data(), &address, dns_callback, &self);
  if(error == ERR_OK)
    self.connect(&address);
  else if(error != ERR_INPROGRESS)
    self.connect(nullptr);
}

void AsyncClient::dns_callback(const char *, const ip_addr_t *address, void *context)
{
  static_cast<AsyncClient *>(context)->connect(address);
}

void AsyncClient::connect(const ip_addr_t *address)
{
  if(address != nullptr)
  {
    if(pcb_ == nullptr)
      pcb_ = udp_new_ip_type(IPADDR_TYPE_V4);
    if(pcb_ != nullptr && udp_connect(pcb_, address, port_) == ERR_OK)
    {
      udp_recv(pcb_, receive_callback, this);
      state_.store(State::Ready, std::memory_order_release);
      return;
    }
  }
  state_.store(State::Failed, std::memory_order_release);
}

esp_err_t AsyncClient::send(const void *data, std::size_t size)
{
  if(data == nullptr || size == 0 || size > tx_.size())
    return ESP_ERR_INVALID_ARG;
  if(!ready())
    return ESP_ERR_INVALID_STATE;
  bool expected = false;
  if(!sending_.compare_exchange_strong(expected, true, std::memory_order_acquire))
    return ESP_ERR_TIMEOUT;
  std::memcpy(tx_.data(), data, size);
  txSize_ = size;
  if(tcpip_try_callback(send_callback, this) != ERR_OK)
  {
    sending_.store(false, std::memory_order_release);
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

void AsyncClient::send_callback(void *context)
{
  auto &self = *static_cast<AsyncClient *>(context);
  auto error = ESP_ERR_NO_MEM;
  if(auto *packet = pbuf_alloc(PBUF_TRANSPORT, self.txSize_, PBUF_RAM))
  {
    if(pbuf_take(packet, self.tx_.data(), self.txSize_) == ERR_OK)
      error = udp_send(self.pcb_, packet) == ERR_OK ? ESP_OK : ESP_FAIL;
    pbuf_free(packet);
  }
  self.sendError_.store(error, std::memory_order_release);
  self.sending_.store(false, std::memory_order_release);
}

void AsyncClient::receive_callback(void *context, udp_pcb *, pbuf *packet, const ip_addr_t *, u16_t)
{
  auto &self = *static_cast<AsyncClient *>(context);
  Rx expected = Rx::Empty;
  if(packet != nullptr && packet->tot_len <= self.rx_.size() &&
     self.received_.compare_exchange_strong(expected, Rx::Writing, std::memory_order_acquire))
  {
    self.rxSize_ = pbuf_copy_partial(packet, self.rx_.data(), packet->tot_len, 0);
    self.received_.store(Rx::Ready, std::memory_order_release);
  }
  if(packet != nullptr)
    pbuf_free(packet);
}

esp_err_t AsyncClient::receive(void *data, std::size_t capacity, std::size_t &size)
{
  size = 0;
  if(!ready() || received_.load(std::memory_order_acquire) != Rx::Ready)
    return ESP_ERR_TIMEOUT;
  const auto error = data != nullptr && capacity >= rxSize_ ? ESP_OK : ESP_ERR_INVALID_SIZE;
  if(error == ESP_OK)
  {
    std::memcpy(data, rx_.data(), rxSize_);
    size = rxSize_;
  }
  received_.store(Rx::Empty, std::memory_order_release);
  return error;
}
} // namespace udp
