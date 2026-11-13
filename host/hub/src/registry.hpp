#pragma once

// Runtime view of every DEX peripheral type, generated at compile time from
// `dex::Peripherals` (dex.hpp). Each entry carries type-erased codecs so the
// rest of the hub handles any peripheral without knowing its structs.

#include "reflect.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hub
{
using json = reflect::json;
using Buffer = std::vector<std::uint8_t>;

struct PeripheralType
{
  std::uint8_t type = 0;
  std::string name;
  std::string description;
  bool hasStatus = false, hasConfig = false, hasAction = false, hasMonitoring = false;

  // Codecs operate on the payload that follows the peripheral ID byte.
  std::function<json(dex::Bytes)> decodeStatus, decodeConfig, decodeMonitoring;
  // Config write: device values + user changes (read-only fields frozen,
  // ranges and the peripheral's DEX `valid()` rule checked before sending).
  std::function<Buffer(const json &current, const json &changes)> encodeConfig;
  std::function<Buffer(const json &)> encodeAction;
  std::function<Buffer(const json &)> defaultAction; // Template for the action form.

  // Monitoring: flattened signals and their values (same order).
  std::size_t monitoringSize = 0;
  std::vector<reflect::Signal> signals;
  std::function<bool(dex::Bytes, std::uint32_t &timestamp, std::vector<double> &values)> sample;
};

// Device-level decoded payloads.
struct DeviceInfo
{
  dex::Version firmware{};
  struct Entry
  {
    std::uint8_t id = 0;
    std::optional<std::uint8_t> type;
    dex::Version version{};
    dex::peripheral::PeripheralCapability capabilities{};
  };
  std::vector<Entry> peripherals;
};

const std::vector<PeripheralType> &peripheral_types();
const PeripheralType *find_type(std::uint8_t type);

DeviceInfo decode_device_info(dex::Bytes data);
json device_info_json(const DeviceInfo &info);

// Device Status: StatusData followed by zero or more peripheral::StatusData.
json decode_device_status(dex::Bytes data, dex::StatusData &summary);

// Fallback when a peripheral type is unknown: its common StatusData prefix.
json decode_generic_status(dex::Bytes data);

// The full protocol description served at /api/schema.
const json &schema();

bool is_peripheral_command(dex::CommandId command);

template <typename E>
std::string enum_name(E value)
{
  const json names = reflect::enum_to_json(value);
  if(names.is_string())
    return names.get<std::string>();
  if(names.is_array())
  {
    std::string out;
    for(const auto &item : names)
      out += (out.empty() ? "" : "|") + (item.is_string() ? item.get<std::string>() : item.dump());
    return out.empty() ? "None" : out;
  }
  return names.dump();
}
} // namespace hub
