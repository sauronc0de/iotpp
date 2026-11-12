#pragma once

// SQLite persistence. Samples use a narrow (signal, ts, value) table so any
// new DEX signal is stored without schema changes; signals are registered on
// first sight. One connection, WAL mode, serialized by a mutex.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

struct sqlite3;

namespace hub
{
using json = nlohmann::ordered_json;

struct Event
{
  std::int64_t id = 0;
  std::int64_t ts = 0; // Unix ms.
  std::string device;
  int peripheral = -1;
  std::string severity; // info | success | warning | error | notification
  std::string title;
  std::string detail;
  bool acked = false;
  json to_json() const;
};

struct DeviceRow
{
  std::string key;
  std::string host;
  int port = 0;
  std::string name;
};

struct Sample
{
  std::int64_t signal;
  std::int64_t ts; // Unix ms.
  double value;
};

class Storage
{
public:
  explicit Storage(const std::string &path);
  ~Storage();
  Storage(const Storage &) = delete;
  Storage &operator=(const Storage &) = delete;

  std::int64_t signal_id(const std::string &device, int peripheral, int type, const std::string &key);
  void insert(const std::vector<Sample> &samples);
  json history(const std::vector<std::int64_t> &ids, std::int64_t from, std::int64_t to, int points);
  json signals(const std::string &device = {});

  std::int64_t add_event(Event &event);
  json events(int limit, std::int64_t beforeId, const std::string &severity, const std::string &device, bool unackedOnly);
  int ack(const std::vector<std::int64_t> &ids); // Empty = all.
  json event_counts();

  void save_device(const DeviceRow &device);
  void remove_device(const std::string &key);
  std::vector<DeviceRow> devices();

  void prune(std::int64_t olderThanMs);
  json stats();

private:
  void exec(const char *sql);

  sqlite3 *db_ = nullptr;
  std::mutex mutex_;
  std::map<std::tuple<std::string, int, std::string>, std::int64_t> signalCache_;
};
} // namespace hub
