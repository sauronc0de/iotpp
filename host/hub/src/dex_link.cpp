#include "dex_link.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <system_error>

namespace hub
{
namespace
{
class Socket
{
public:
  Socket() : fd_(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0))
  {
    if(fd_ < 0)
      throw std::system_error(errno, std::generic_category(), "socket");
  }
  ~Socket() { close(fd_); }
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
  int get() const { return fd_; }

private:
  int fd_;
};

sockaddr_in address_of(const Endpoint &endpoint)
{
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = endpoint.ip;
  address.sin_port = htons(endpoint.port);
  return address;
}
} // namespace

std::string Endpoint::address() const
{
  char text[INET_ADDRSTRLEN]{};
  in_addr addr{ip};
  inet_ntop(AF_INET, &addr, text, sizeof(text));
  return text;
}

std::optional<Endpoint> Endpoint::resolve(const std::string &host, std::uint16_t port)
{
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *result = nullptr;
  if(getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
    return std::nullopt;
  Endpoint endpoint{reinterpret_cast<const sockaddr_in *>(result->ai_addr)->sin_addr.s_addr, port};
  freeaddrinfo(result);
  return endpoint;
}

DexLink::DexLink(std::uint16_t listenPort, Handler unsolicited) : unsolicited_(std::move(unsolicited))
{
  fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if(fd_ < 0)
    throw std::system_error(errno, std::generic_category(), "socket");
  const int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(listenPort);
  if(bind(fd_, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) < 0)
  {
    const auto error = errno;
    close(fd_);
    throw std::system_error(error, std::generic_category(), "UDP bind on port " + std::to_string(listenPort));
  }
  thread_ = std::thread([this] { receive_loop(); });
}

DexLink::~DexLink()
{
  running_ = false;
  if(thread_.joinable())
    thread_.join();
  close(fd_);
}

void DexLink::count(std::uint64_t Counters::*counter)
{
  std::lock_guard lock(mutex_);
  ++(counters_.*counter);
}

std::shared_ptr<std::mutex> DexLink::device_lock(std::uint32_t ip)
{
  std::lock_guard lock(mutex_);
  auto &entry = deviceLocks_[ip];
  if(!entry)
    entry = std::make_shared<std::mutex>();
  return entry;
}

std::optional<Reply> DexLink::request(const Endpoint &to, dex::CommandId command, std::span<const std::uint8_t> payload,
                                      std::chrono::milliseconds timeout, std::vector<std::uint8_t> *sent)
{
  std::array<std::uint8_t, dex::MaxDatagramSize> packet{};
  const auto encoded = dex::encode_request({command, {payload.data(), payload.size()}}, packet.data(), packet.size());
  if(!encoded)
    throw std::invalid_argument("Request does not fit in a DEX datagram");
  if(sent)
    sent->assign(packet.begin(), packet.begin() + encoded.size);

  const auto serial = device_lock(to.ip);
  std::lock_guard deviceGuard(*serial);

  // A fresh socket per exchange, connected to the device: the kernel only
  // delivers datagrams from that peer, and replies to earlier exchanges hit
  // sockets that no longer exist.
  Socket udp;
  const auto target = address_of(to);
  if(connect(udp.get(), reinterpret_cast<const sockaddr *>(&target), sizeof(target)) < 0 ||
     send(udp.get(), packet.data(), encoded.size, 0) != static_cast<ssize_t>(encoded.size))
  {
    count(&Counters::timeouts);
    return std::nullopt;
  }
  count(&Counters::sent);

  std::array<std::uint8_t, dex::MaxDatagramSize + 1> buffer{};
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for(;;)
  {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    pollfd descriptor{udp.get(), POLLIN, 0};
    if(left.count() <= 0 || poll(&descriptor, 1, static_cast<int>(left.count())) <= 0)
      break;
    const auto received = recv(udp.get(), buffer.data(), buffer.size(), 0);
    if(received < 0)
      break; // e.g. ECONNREFUSED: nothing listens there, fail fast.
    dex::Response response;
    if(dex::decode_response({buffer.data(), static_cast<std::size_t>(received)}, response) != dex::Error::None ||
       response.command != command)
    {
      count(&Counters::invalid);
      continue;
    }
    count(&Counters::received);
    return Reply{response.result, {response.data.data, response.data.data + response.data.size},
                 {buffer.data(), buffer.data() + received}};
  }
  count(&Counters::timeouts);
  return std::nullopt;
}

void DexLink::broadcast(dex::CommandId command, std::uint16_t port)
{
  // From the listening socket: discovery replies arrive there as unsolicited Info.
  const auto header = static_cast<std::uint8_t>(command);
  const auto target = address_of({htonl(INADDR_BROADCAST), port});
  if(sendto(fd_, &header, 1, 0, reinterpret_cast<const sockaddr *>(&target), sizeof(target)) == 1)
    count(&Counters::sent);
}

DexLink::Counters DexLink::counters() const
{
  std::lock_guard lock(mutex_);
  return counters_;
}

void DexLink::receive_loop()
{
  std::array<std::uint8_t, dex::MaxDatagramSize + 1> buffer{};
  while(running_)
  {
    pollfd descriptor{fd_, POLLIN, 0};
    if(poll(&descriptor, 1, 250) <= 0)
      continue;
    sockaddr_in sender{};
    socklen_t size = sizeof(sender);
    const auto received = recvfrom(fd_, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr *>(&sender), &size);
    if(received <= 0)
      continue;
    dex::Response response;
    if(dex::decode_response({buffer.data(), static_cast<std::size_t>(received)}, response) != dex::Error::None)
    {
      count(&Counters::invalid);
      continue;
    }
    count(&Counters::received);
    const Reply reply{response.result, {response.data.data, response.data.data + response.data.size},
                      {buffer.data(), buffer.data() + received}};
    if(unsolicited_)
      unsolicited_({sender.sin_addr.s_addr, ntohs(sender.sin_port)}, response.command, reply);
  }
}
} // namespace hub
