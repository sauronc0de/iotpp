#include "devices.hpp"

#include <algorithm>
#include <cstdio>
#include <tuple>
#include <future>
#include <iostream>

namespace hub
{
namespace
{
std::int64_t now_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string hex(const Buffer &bytes)
{
  std::string out;
  out.reserve(bytes.size() * 3);
  char item[4];
  for(const auto byte : bytes)
  {
    std::snprintf(item, sizeof(item), out.empty() ? "%02x" : " %02x", byte);
    out += item;
  }
  return out;
}

std::string version_text(const dex::Version &v)
{
  return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

// Names of the flags set in `now` but not in `before`.
template <typename E>
std::vector<std::string> raised(E now, E before)
{
  using U = std::underlying_type_t<E>;
  std::vector<std::string> names;
  for(const auto flag : magic_enum::enum_values<E>())
  {
    const auto bit = static_cast<U>(flag);
    if(bit != 0 && (static_cast<U>(now) & bit) == bit && (static_cast<U>(before) & bit) != bit)
      names.emplace_back(magic_enum::enum_name(flag));
  }
  return names;
}

std::string enum_value_name(const std::string &enumName, double value)
{
  const auto &enums = schema()["enums"];
  if(const auto it = enums.find(enumName); it != enums.end())
    for(const auto &entry : (*it)["values"])
      if(entry["value"].get<double>() == value)
        return entry["name"].get<std::string>();
  return std::to_string(static_cast<long long>(value));
}

bool read_only(dex::CommandId command, const json &data)
{
  using C = dex::CommandId;
  return command == C::Info || command == C::Status || command == C::PeripheralStatus ||
         command == C::PeripheralMonitoring ||
         ((command == C::Config || command == C::PeripheralConfig) && data.is_null());
}
} // namespace

DeviceManager::DeviceManager(Storage &storage, EventBus &bus, DeviceOptions options)
    : storage_(storage), bus_(bus), options_(options)
{
  for(const auto &row : storage_.devices())
    if(const auto endpoint = Endpoint::resolve(row.host, static_cast<std::uint16_t>(row.port)))
    {
      Device device;
      device.key = row.key;
      device.name = row.name;
      device.endpoint = *endpoint;
      devices_.emplace(row.key, std::move(device));
    }
}

DeviceManager::~DeviceManager() { stop(); }

void DeviceManager::attach(DexLink &link)
{
  link_ = &link;
  running_ = true;
  poller_ = std::thread([this] { poll_loop(); });
}

void DeviceManager::stop()
{
  running_ = false;
  wake_.notify_all();
  if(poller_.joinable())
    poller_.join();
}

// ---- Inventory -------------------------------------------------------------------
DeviceManager::Device &DeviceManager::ensure(const Endpoint &endpoint, std::uint16_t port)
{
  const auto key = endpoint.address();
  auto [it, created] = devices_.try_emplace(key);
  auto &device = it->second;
  if(created)
  {
    device.key = key;
    device.endpoint = {endpoint.ip, port};
    storage_.save_device({key, key, port, ""});
    emit(device, -1, "info", "New device discovered", key + ":" + std::to_string(port));
    wake_.notify_all();
  }
  return device;
}

DeviceManager::Peripheral *DeviceManager::peripheral(Device &device, std::uint8_t id)
{
  for(auto &p : device.peripherals)
    if(p.id == id)
      return &p;
  return nullptr;
}

const PeripheralType *DeviceManager::type_of(const Peripheral &p) const
{
  return p.type ? find_type(*p.type) : nullptr;
}

std::string DeviceManager::label(const Device &device, int pid) const
{
  std::string text = device.name.empty() ? device.key : device.name;
  if(pid < 0)
    return text;
  for(const auto &p : device.peripherals)
    if(p.id == pid)
      if(const auto *t = type_of(p))
        return text + " · " + t->name + " #" + std::to_string(pid);
  return text + " · #" + std::to_string(pid);
}

json DeviceManager::list()
{
  std::lock_guard lock(mutex_);
  json out = json::array();
  for(const auto &[key, device] : devices_)
    out.push_back(device_json(device));
  return out;
}

std::optional<json> DeviceManager::get(const std::string &key)
{
  std::lock_guard lock(mutex_);
  const auto it = devices_.find(key);
  if(it == devices_.end())
    return std::nullopt;
  return device_json(it->second);
}

json DeviceManager::add(const std::string &host, std::uint16_t port, const std::string &name)
{
  const auto endpoint = Endpoint::resolve(host, port ? port : options_.devicePort);
  if(!endpoint)
    throw reflect::Error("Cannot resolve " + host);
  std::lock_guard lock(mutex_);
  auto &device = ensure(*endpoint, endpoint->port);
  device.endpoint = *endpoint;
  if(!name.empty())
    device.name = name;
  device.nextPoll = 0;
  storage_.save_device({device.key, device.key, endpoint->port, device.name});
  wake_.notify_all();
  publish_device(device);
  return device_json(device);
}

bool DeviceManager::rename(const std::string &key, const std::string &name)
{
  std::lock_guard lock(mutex_);
  const auto it = devices_.find(key);
  if(it == devices_.end())
    return false;
  it->second.name = name;
  storage_.save_device({key, key, it->second.endpoint.port, name});
  publish_device(it->second);
  return true;
}

bool DeviceManager::remove(const std::string &key)
{
  std::lock_guard lock(mutex_);
  if(devices_.erase(key) == 0)
    return false;
  storage_.remove_device(key);
  bus_.publish("removed", {{"key", key}});
  return true;
}

void DeviceManager::discover()
{
  if(link_)
    link_->broadcast(dex::CommandId::Info, options_.devicePort);
}

void DeviceManager::refresh(const std::string &key)
{
  {
    std::lock_guard lock(mutex_);
    const auto it = devices_.find(key);
    if(it == devices_.end())
      throw std::out_of_range("Unknown device " + key);
    it->second.infoAt = 0;
    for(auto &p : it->second.peripherals)
    {
      p.config = nullptr;
      p.statusUnsupported = p.configUnsupported = false;
    }
  }
  poll(key);
}

// ---- Commands --------------------------------------------------------------------
json DeviceManager::execute(const std::string &key, dex::CommandId command, std::optional<std::uint8_t> pid,
                            const json &data)
{
  Buffer payload;
  if(is_peripheral_command(command))
  {
    if(!pid)
      throw reflect::Error("Peripheral commands need a peripheral id");
    payload.push_back(*pid);
    const bool writesConfig = command == dex::CommandId::PeripheralConfig && !data.is_null();
    if(writesConfig || command == dex::CommandId::PeripheralAction)
    {
      const PeripheralType *type = nullptr;
      json current;
      const auto load = [&] {
        std::lock_guard lock(mutex_);
        const auto it = devices_.find(key);
        if(it == devices_.end())
          throw std::out_of_range("Unknown device " + key);
        const auto *p = peripheral(it->second, *pid);
        if(!p)
          throw reflect::Error("Unknown peripheral " + std::to_string(*pid) + " (query Info first)");
        type = type_of(*p);
        current = p->config;
      };
      load();
      if(!type)
        throw reflect::Error("Peripheral type unknown to this hub build");
      Buffer body;
      if(writesConfig)
      {
        if(!type->hasConfig)
          throw reflect::Error(type->name + " has no configuration");
        if(current.is_null()) // Writes are full structs: start from the device's values.
        {
          execute(key, dex::CommandId::PeripheralConfig, pid);
          load();
        }
        current.merge_patch(data);
        body = type->encodeConfig(current);
      }
      else
      {
        if(!type->hasAction)
          throw reflect::Error(type->name + " has no actions");
        body = type->encodeAction(data.is_null() ? json::object() : data);
      }
      payload.insert(payload.end(), body.begin(), body.end());
    }
  }
  else if(command == dex::CommandId::Config && !data.is_null())
  {
    const auto config = reflect::parse<dex::device::ConfigData>(data);
    if constexpr(!std::is_empty_v<dex::device::ConfigData>)
      payload.insert(payload.end(), reinterpret_cast<const std::uint8_t *>(&config),
                     reinterpret_cast<const std::uint8_t *>(&config) + sizeof(config));
  }
  return execute_payload(key, command, payload, read_only(command, data));
}

json DeviceManager::execute_raw(const std::string &key, dex::CommandId command, const Buffer &payload)
{
  return execute_payload(key, command, payload, false);
}

json DeviceManager::execute_payload(const std::string &key, dex::CommandId command, const Buffer &payload, bool retry)
{
  if(!link_)
    throw std::logic_error("DEX link not attached");
  Endpoint endpoint;
  {
    std::lock_guard lock(mutex_);
    const auto it = devices_.find(key);
    if(it == devices_.end())
      throw std::out_of_range("Unknown device " + key);
    endpoint = it->second.endpoint;
  }
  Buffer sent;
  const auto start = std::chrono::steady_clock::now();
  auto reply = link_->request(endpoint, command, payload, options_.timeout, &sent);
  if(!reply && retry)
    reply = link_->request(endpoint, command, payload, options_.timeout, &sent);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  json out = {{"command", magic_enum::enum_name(command)}, {"request", hex(sent)}, {"elapsedMs", elapsed.count()}};
  std::lock_guard lock(mutex_);
  const auto it = devices_.find(key);
  if(it == devices_.end())
    throw std::out_of_range("Device removed");
  auto &device = it->second;
  if(!reply)
  {
    failed(device);
    out["ok"] = false;
    out["timeout"] = true;
    out["error"] = "No response within " + std::to_string(options_.timeout.count()) + " ms";
    publish_device(device);
    return out;
  }
  out["response"] = hex(reply->raw);
  out["result"] = reflect::enum_to_json(reply->result);
  out["ok"] = reply->result == dex::Error::None;
  if(!payload.empty() && is_peripheral_command(command) && reply->result != dex::Error::None)
    if(auto *p = peripheral(device, payload[0]))
    {
      p->lastError = enum_name(reply->result);
      const bool unsupported = (static_cast<std::uint32_t>(reply->result) &
                                static_cast<std::uint32_t>(dex::Error::UnsupportedOperation)) != 0;
      if(unsupported && command == dex::CommandId::PeripheralStatus)
        p->statusUnsupported = true;
      if(unsupported && command == dex::CommandId::PeripheralConfig)
        p->configUnsupported = true;
    }
  try
  {
    out["data"] = apply(device, command, *reply);
  }
  catch(const std::exception &error)
  {
    out["decodeError"] = error.what();
  }
  publish_device(device);
  return out;
}

// ---- Responses / publications ---------------------------------------------------
void DeviceManager::on_datagram(const Endpoint &from, dex::CommandId command, const Reply &reply)
{
  std::lock_guard lock(mutex_);
  auto it = devices_.find(from.address());
  Device *device = it == devices_.end() ? nullptr : &it->second;
  if(!device)
  {
    using C = dex::CommandId;
    if(command != C::PeripheralMonitoring && command != C::Info && command != C::Status && command != C::PeripheralStatus)
      return;
    // Discovery replies come from the DEX server port; publications from an ephemeral one.
    device = &ensure(from, command == C::Info ? from.port : options_.devicePort);
  }
  try
  {
    apply(*device, command, reply);
  }
  catch(const std::exception &error)
  {
    std::cerr << "Ignoring " << magic_enum::enum_name(command) << " from " << from.address() << ": " << error.what() << '\n';
  }
  if(command != dex::CommandId::PeripheralMonitoring)
    publish_device(*device);
}

json DeviceManager::apply(Device &device, dex::CommandId command, const Reply &reply)
{
  seen(device);
  if(reply.result != dex::Error::None)
    return nullptr;
  const auto bytes = reply.bytes();
  using C = dex::CommandId;
  switch(command)
  {
  case C::Info:
  {
    const auto info = decode_device_info(bytes);
    device.firmware = info.firmware;
    device.infoAt = now_ms();
    std::vector<Peripheral> peripherals;
    for(const auto &entry : info.peripherals)
    {
      Peripheral p;
      if(const auto *existing = peripheral(device, entry.id))
        p = *existing;
      p.id = entry.id;
      if(entry.type)
        p.type = entry.type;
      p.version = entry.version;
      p.capabilities = entry.capabilities;
      peripherals.push_back(std::move(p));
    }
    // Keep peripherals known only from publications (legacy Info without them).
    for(auto &p : device.peripherals)
      if(std::none_of(peripherals.begin(), peripherals.end(), [&](const Peripheral &q) { return q.id == p.id; }))
        peripherals.push_back(std::move(p));
    device.peripherals = std::move(peripherals);
    return device_info_json(info);
  }
  case C::Status:
  {
    dex::StatusData summary{};
    device.status = decode_device_status(bytes, summary);
    const auto before = device.summary;
    device.summary = summary;
    diff_status(device, -1, before, summary);
    return device.status;
  }
  case C::Config:
    return json::object();
  case C::Reset:
    return {{"restarting", true}};
  default:
    break;
  }

  if(bytes.size == 0)
    return nullptr;
  const std::uint8_t pid = bytes.data[0];
  const dex::Bytes rest{bytes.data + 1, bytes.size - 1};
  auto *p = peripheral(device, pid);
  if(!p)
  {
    device.peripherals.push_back({});
    p = &device.peripherals.back();
    p->id = pid;
  }
  const auto *type = type_of(*p);
  switch(command)
  {
  case C::PeripheralStatus:
  {
    p->status = type && type->hasStatus ? type->decodeStatus(rest) : decode_generic_status(rest);
    p->statusAt = now_ms();
    p->lastError.clear();
    dex::StatusData summary{};
    std::memcpy(&summary, rest.data, sizeof(summary)); // Every peripheral status starts with it.
    const auto before = p->summary;
    p->summary = summary;
    diff_status(device, pid, before, summary);
    return p->status;
  }
  case C::PeripheralConfig:
    if(!type || !type->hasConfig)
      return {{"peripheral", pid}, {"hex", hex({rest.data, rest.data + rest.size})}};
    p->config = type->decodeConfig(rest);
    p->configAt = now_ms();
    p->lastError.clear();
    return p->config;
  case C::PeripheralMonitoring:
    return ingest(device, pid, rest);
  case C::PeripheralAction:
    p->lastError.clear();
    return {{"peripheral", pid}, {"accepted", true}};
  default:
    return {{"peripheral", pid}, {"hex", hex({rest.data, rest.data + rest.size})}};
  }
}

json DeviceManager::ingest(Device &device, std::uint8_t pid, dex::Bytes payload)
{
  auto *p = peripheral(device, pid);
  const auto *type = type_of(*p);
  if(!type || !type->hasMonitoring || type->monitoringSize != payload.size)
  {
    // Publications carry no type tag: infer it from a unique payload size.
    const PeripheralType *match = nullptr;
    int matches = 0;
    for(const auto &candidate : peripheral_types())
      if(candidate.hasMonitoring && candidate.monitoringSize == payload.size)
        match = &candidate, ++matches;
    if(matches != 1 || (p->type && type))
      throw reflect::Error("Unrecognized monitoring payload of " + std::to_string(payload.size) + " bytes");
    p->type = match->type;
    type = match;
  }
  std::uint32_t timestamp = 0;
  std::vector<double> values;
  type->sample(payload, timestamp, values);
  const std::int64_t ts = timestamp ? timestamp * 1000LL : now_ms();

  std::vector<Sample> rows;
  rows.reserve(values.size());
  json live = json::object();
  for(std::size_t i = 0; i < values.size(); ++i)
  {
    const auto &signal = type->signals[i];
    const auto id = storage_.signal_id(device.key, pid, type->type, signal.key);
    rows.push_back({id, ts, values[i]});
    live[std::to_string(id)] = values[i];
    if(signal.kind != "number")
    {
      // Discrete signals (door open/closed...) also become timeline events.
      const auto memo = std::to_string(pid) + ":" + signal.key;
      const auto previous = device.lastEnum.find(memo);
      if(previous != device.lastEnum.end() && previous->second != values[i])
      {
        const auto text = signal.kind == "bool" ? (values[i] != 0 ? "On" : "Off") : enum_value_name(signal.enumName, values[i]);
        emit(device, pid, "info", signal.label + ": " + text, label(device, pid));
      }
      device.lastEnum[memo] = values[i];
    }
  }
  storage_.insert(rows);
  p->monitoring = type->decodeMonitoring(payload);
  p->monitoringAt = now_ms();
  device.samples += rows.size();
  ++device.publications;
  bus_.publish("samples", {{"device", device.key}, {"peripheral", pid}, {"ts", ts}, {"values", std::move(live)}});
  return p->monitoring;
}

// ---- Health and notifications ---------------------------------------------------
void DeviceManager::seen(Device &device)
{
  device.lastSeen = now_ms();
  device.failures = 0;
  if(!device.online)
  {
    device.online = true;
    emit(device, -1, "success", "Online", label(device, -1));
  }
}

void DeviceManager::failed(Device &device)
{
  if(++device.failures >= options_.offlineAfter && device.online)
  {
    device.online = false;
    emit(device, -1, "warning", "Offline", label(device, -1) + " stopped answering");
  }
}

void DeviceManager::diff_status(Device &device, int pid, const std::optional<dex::StatusData> &before,
                                const dex::StatusData &now)
{
  const dex::StatusData previous = before.value_or(dex::StatusData{dex::State::None, dex::Error::None, dex::Notification::None});
  const auto where = label(device, pid);
  for(const auto &name : raised(now.notifications, previous.notifications))
    emit(device, pid, "notification", name, where);
  for(const auto &name : raised(now.errors, previous.errors))
    emit(device, pid, "error", name, where);
  if(before)
    for(const auto &name : raised(previous.errors, now.errors))
      emit(device, pid, "success", "Recovered: " + name, where);
  for(const auto &name : raised(now.state, previous.state))
    if(name == "Fault")
      emit(device, pid, "error", "Fault", where);
    else if(name == "Warning" || name == "Degraded")
      emit(device, pid, "warning", name, where);
}

void DeviceManager::emit(Device &device, int pid, const std::string &severity, const std::string &title,
                         const std::string &detail)
{
  Event event{0, now_ms(), device.key, pid, severity, title, detail, false};
  storage_.add_event(event);
  auto payload = event.to_json();
  payload["deviceName"] = device.name.empty() ? device.key : device.name;
  bus_.publish("event", payload);
}

// ---- Serialization --------------------------------------------------------------
json DeviceManager::device_json(const Device &device) const
{
  json peripherals = json::array();
  for(const auto &p : device.peripherals)
  {
    const auto *type = type_of(p);
    json entry = {{"id", p.id},
                  {"version", version_text(p.version)},
                  {"capabilities", reflect::enum_to_json(p.capabilities)},
                  {"status", p.status},
                  {"config", p.config},
                  {"monitoring", p.monitoring},
                  {"statusAt", p.statusAt},
                  {"configAt", p.configAt},
                  {"monitoringAt", p.monitoringAt},
                  {"statusUnsupported", p.statusUnsupported},
                  {"configUnsupported", p.configUnsupported},
                  {"lastError", p.lastError}};
    entry["type"] = p.type ? json(*p.type) : json();
    entry["typeName"] = type ? json(type->name) : json();
    peripherals.push_back(std::move(entry));
  }
  json out = {{"key", device.key},
              {"name", device.name},
              {"host", device.endpoint.address()},
              {"port", device.endpoint.port},
              {"online", device.online},
              {"lastSeen", device.lastSeen},
              {"failures", device.failures},
              {"status", device.status},
              {"peripherals", std::move(peripherals)},
              {"samples", device.samples},
              {"publications", device.publications}};
  out["firmware"] = device.firmware ? json(version_text(*device.firmware)) : json();
  return out;
}

void DeviceManager::publish_device(const Device &device) { bus_.publish("device", device_json(device)); }

json DeviceManager::stats()
{
  std::lock_guard lock(mutex_);
  std::size_t online = 0;
  std::uint64_t samples = 0;
  for(const auto &[key, device] : devices_)
    online += device.online, samples += device.samples;
  json out = {{"devices", devices_.size()}, {"online", online}, {"samplesSinceStart", samples}};
  if(link_)
  {
    const auto c = link_->counters();
    out["udp"] = {{"received", c.received}, {"sent", c.sent}, {"invalid", c.invalid}, {"timeouts", c.timeouts}};
  }
  return out;
}

// ---- Polling --------------------------------------------------------------------
void DeviceManager::poll_loop()
{
  while(running_)
  {
    const auto now = now_ms();
    std::vector<std::string> due;
    {
      std::lock_guard lock(mutex_);
      for(auto &[key, device] : devices_)
        if(device.nextPoll <= now)
        {
          due.push_back(key);
          const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(options_.pollInterval).count();
          device.nextPoll = now + (device.online || device.lastSeen == 0 ? interval : std::max<std::int64_t>(interval * 6, 30000));
        }
    }
    std::vector<std::future<void>> jobs;
    for(const auto &key : due)
      jobs.push_back(std::async(std::launch::async, [this, key] {
        try
        {
          poll(key);
        }
        catch(const std::exception &error)
        {
          std::cerr << "Poll " << key << ": " << error.what() << '\n';
        }
      }));
    for(auto &job : jobs)
      job.wait();
    std::unique_lock lock(wakeMutex_);
    wake_.wait_for(lock, std::chrono::seconds(1));
  }
}

void DeviceManager::poll(const std::string &key)
{
  bool needInfo = false;
  {
    std::lock_guard lock(mutex_);
    const auto it = devices_.find(key);
    if(it == devices_.end())
      return;
    needInfo = !it->second.firmware || now_ms() - it->second.infoAt > options_.infoInterval.count() * 1000;
  }
  if(needInfo)
    execute(key, dex::CommandId::Info, std::nullopt);
  const auto statusReply = execute(key, dex::CommandId::Status, std::nullopt);
  if(!statusReply.value("ok", false))
    return;

  std::vector<std::tuple<std::uint8_t, bool, bool>> targets; // (id, poll status, read config)
  {
    std::lock_guard lock(mutex_);
    const auto it = devices_.find(key);
    if(it == devices_.end())
      return;
    for(const auto &p : it->second.peripherals)
      if(const auto *type = type_of(p))
      {
        const bool status = type->hasStatus && !p.statusUnsupported;
        const bool config = type->hasConfig && !p.configUnsupported && p.config.is_null();
        if(status || config)
          targets.emplace_back(p.id, status, config);
      }
  }
  for(const auto &[id, status, config] : targets)
  {
    if(status)
      execute(key, dex::CommandId::PeripheralStatus, id);
    if(config)
      execute(key, dex::CommandId::PeripheralConfig, id);
  }
}
} // namespace hub
