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

bool DexLink::send_to(const Endpoint &to, std::span<const std::uint8_t> bytes)
{
  sockaddr_in target{};
  target.sin_family = AF_INET;
  target.sin_addr.s_addr = to.ip;
  target.sin_port = htons(to.port);
  const auto sent = sendto(fd_, bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr *>(&target), sizeof(target));
  std::lock_guard lock(mutex_);
  if(sent == static_cast<ssize_t>(bytes.size()))
    ++counters_.sent;
  return sent == static_cast<ssize_t>(bytes.size());
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

  // One outstanding request per device: replies carry no sequence number.
  const auto serial = device_lock(to.ip);
  std::lock_guard deviceGuard(*serial);
  std::future<Reply> future;
  std::uint64_t id = 0;
  {
    std::lock_guard lock(mutex_);
    id = ++nextId_;
    future = pending_.insert(pending_.end(), Pending{id, to.ip, command, {}})->promise.get_future();
  }
  std::optional<Reply> reply;
  if(send_to(to, {packet.data(), encoded.size}) && future.wait_for(timeout) == std::future_status::ready)
    reply = future.get();
  std::lock_guard lock(mutex_);
  if(!reply)
  {
    ++counters_.timeouts;
    // The receive thread may have fulfilled it just now; erase only if still queued.
    pending_.remove_if([id](const Pending &p) { return p.id == id; });
  }
  return reply;
}

void DexLink::broadcast(dex::CommandId command, std::uint16_t port)
{
  const auto header = static_cast<std::uint8_t>(command);
  send_to({htonl(INADDR_BROADCAST), port}, {&header, 1});
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
      std::lock_guard lock(mutex_);
      ++counters_.invalid;
      continue;
    }
    Reply reply{response.result, {response.data.data, response.data.data + response.data.size},
                {buffer.data(), buffer.data() + received}};
    const Endpoint from{sender.sin_addr.s_addr, ntohs(sender.sin_port)};
    bool matched = false;
    {
      std::lock_guard lock(mutex_);
      ++counters_.received;
      for(auto it = pending_.begin(); it != pending_.end(); ++it)
        if(it->ip == from.ip && it->command == response.command)
        {
          it->promise.set_value(reply);
          pending_.erase(it);
          matched = true;
          break;
        }
    }
    if(!matched && unsolicited_)
      unsolicited_(from, response.command, reply);
  }
}
} // namespace hub
