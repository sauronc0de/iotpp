#pragma once

// UDP transport for DEX. One socket serves both directions: requests are sent
// from it and replies are matched by (device IP, command); everything else
// (monitoring publications, pushed status, discovery replies) is forwarded
// to the unsolicited handler. Requests to the same device are serialized.

#include "dex.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace hub
{
struct Endpoint
{
  std::uint32_t ip = 0; // Network byte order.
  std::uint16_t port = 0;

  std::string address() const;
  static std::optional<Endpoint> resolve(const std::string &host, std::uint16_t port);
  bool operator==(const Endpoint &) const = default;
};

struct Reply
{
  dex::Error result = dex::Error::None;
  std::vector<std::uint8_t> data; // DATA after the response header.
  std::vector<std::uint8_t> raw;  // Whole datagram.
  dex::Bytes bytes() const { return {data.data(), data.size()}; }
};

class DexLink
{
public:
  using Handler = std::function<void(const Endpoint &from, dex::CommandId, const Reply &)>;

  DexLink(std::uint16_t listenPort, Handler unsolicited);
  ~DexLink();
  DexLink(const DexLink &) = delete;
  DexLink &operator=(const DexLink &) = delete;

  // Blocking request; std::nullopt on timeout. `sent` receives the raw request.
  std::optional<Reply> request(const Endpoint &to, dex::CommandId command, std::span<const std::uint8_t> payload,
                               std::chrono::milliseconds timeout, std::vector<std::uint8_t> *sent = nullptr);
  void broadcast(dex::CommandId command, std::uint16_t port);

  struct Counters
  {
    std::uint64_t received = 0, sent = 0, invalid = 0, timeouts = 0;
  };
  Counters counters() const;

private:
  struct Pending
  {
    std::uint64_t id;
    std::uint32_t ip;
    dex::CommandId command;
    std::promise<Reply> promise;
  };

  void receive_loop();
  std::shared_ptr<std::mutex> device_lock(std::uint32_t ip);
  bool send_to(const Endpoint &to, std::span<const std::uint8_t> bytes);

  int fd_ = -1;
  Handler unsolicited_;
  std::atomic<bool> running_{true};
  mutable std::mutex mutex_;
  std::list<Pending> pending_;
  std::uint64_t nextId_ = 0;
  std::map<std::uint32_t, std::shared_ptr<std::mutex>> deviceLocks_;
  Counters counters_;
  std::thread thread_;
};
} // namespace hub
