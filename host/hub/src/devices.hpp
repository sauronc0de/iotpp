#pragma once

// Device inventory and DEX orchestration: polling, generic command
// execution, monitoring ingestion, status diffing into notifications.
// Peripheral specifics come exclusively from the registry.

#include "dex_link.hpp"
#include "event_bus.hpp"
#include "registry.hpp"
#include "storage.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace hub
{
struct DeviceOptions
{
  std::uint16_t devicePort = 4444;
  std::chrono::milliseconds timeout{1500};
  std::chrono::seconds pollInterval{5};
  std::chrono::seconds infoInterval{300};
  int offlineAfter = 3; // Consecutive failed polls.
  std::size_t pollWorkers = 8; // Upper bound of concurrent device polls.
};

class DeviceManager
{
public:
  DeviceManager(Storage &storage, EventBus &bus, DeviceOptions options);
  ~DeviceManager();

  void attach(DexLink &link); // Starts polling.
  void stop();

  // DexLink unsolicited handler: publications, pushes, discovery replies.
  void on_datagram(const Endpoint &from, dex::CommandId command, const Reply &reply);

  json list();
  std::optional<json> get(const std::string &key);
  json add(const std::string &host, std::uint16_t port, const std::string &name);
  bool rename(const std::string &key, const std::string &name);
  bool remove(const std::string &key);
  void discover();
  void refresh(const std::string &key);

  // Any DEX command. `data` (optional) is the JSON payload for Config/Action.
  json execute(const std::string &key, dex::CommandId command, std::optional<std::uint8_t> peripheral,
               const json &data = nullptr);
  // Raw payload bytes (protocol console).
  json execute_raw(const std::string &key, dex::CommandId command, const Buffer &payload);

  json stats();

private:
  struct Peripheral
  {
    std::uint8_t id = 0;
    std::optional<std::uint8_t> type;
    dex::Version version{};
    dex::peripheral::PeripheralCapability capabilities{};
    json status, config, monitoring;
    std::int64_t statusAt = 0, configAt = 0, monitoringAt = 0;
    std::optional<dex::StatusData> summary;
    bool statusUnsupported = false, configUnsupported = false;
    std::string lastError;
  };

  struct Device
  {
    std::string key, name;
    Endpoint endpoint;
    bool online = false;
    int failures = 0;
    std::int64_t lastSeen = 0, infoAt = 0, nextPoll = 0;
    std::optional<dex::Version> firmware;
    json status;
    std::optional<dex::StatusData> summary;
    std::vector<Peripheral> peripherals;
    std::map<std::string, double> lastEnum; // "<pid>:<signal>" -> value
    std::uint64_t samples = 0, publications = 0;
  };

  json execute_payload(const std::string &key, dex::CommandId command, const Buffer &payload, bool retry);
  json apply(Device &device, dex::CommandId command, const Reply &reply);
  json ingest(Device &device, std::uint8_t pid, dex::Bytes payload);
  void seen(Device &device);
  void failed(Device &device);
  void diff_status(Device &device, int pid, const std::optional<dex::StatusData> &before, const dex::StatusData &now);
  void emit(Device &device, int pid, const std::string &severity, const std::string &title, const std::string &detail);
  Device &ensure(const Endpoint &endpoint, std::uint16_t port);
  Peripheral *peripheral(Device &device, std::uint8_t id);
  const PeripheralType *type_of(const Peripheral &p) const;
  std::string label(const Device &device, int pid) const;
  json device_json(const Device &device) const;
  void publish_device(const Device &device);
  void poll_loop();
  void poll(const std::string &key);

  Storage &storage_;
  EventBus &bus_;
  DeviceOptions options_;
  DexLink *link_ = nullptr;
  std::mutex mutex_;
  std::map<std::string, Device> devices_;
  std::atomic<bool> running_{false};
  std::condition_variable_any wake_;
  std::mutex wakeMutex_;
  std::thread poller_;
};
} // namespace hub
