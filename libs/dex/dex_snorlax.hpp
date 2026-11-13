#pragma once

#include "dex_peripheral.hpp"

namespace dex::Snorlax
{
inline constexpr std::uint8_t Type = 143;
#pragma pack(push, 1)
enum class State : std::uint8_t
{
  Closed = 0,
  Open = 1,
  Unknown = 0xFF // No opening or confirmed closure has been observed yet.
};

// Published with peripheral Monitoring, like the other sensor publications.
using MonitoringPublication = dex::MonitoringPublication<State>;

struct ConfigData : dex::peripheral::ConfigData
{
  std::uint8_t enabled = 1; // 0 = disabled, 1 = enabled (door monitoring and Deep Sleep)
};
struct InfoData : dex::peripheral::InfoData
{
  std::uint8_t type = Type;
};
struct StatusData : dex::peripheral::StatusData
{
  State doorState = State::Unknown;
};
#pragma pack(pop)
static_assert(sizeof(MonitoringPublication) == 5);

// ---- Metadata (see dex_meta.hpp) ------------------------------------------
constexpr auto dex_describe(const ConfigData *)
{
  using Self = ConfigData;
  return meta::fields(meta::base<peripheral::ConfigData>(),
                      meta::at("monitoring").readonly().describe("Fixed acquisition profile"),
                      DEX_FIELD(enabled).boolean().doc("Door monitoring and Deep Sleep"));
}

constexpr auto dex_describe(const StatusData *)
{
  using Self = StatusData;
  return meta::fields(meta::base<peripheral::StatusData>(), DEX_FIELD(doorState));
}

struct Peripheral
{
  static constexpr std::uint8_t type = Type;
  static constexpr const char *name = "Snorlax";
  static constexpr const char *description = "Door sensor with Deep Sleep";
  static constexpr const char *signal = "door"; // Name of the scalar monitoring signal.
  using Status = StatusData;
  using Config = ConfigData;
  using Action = meta::None;
  using Monitoring = MonitoringPublication;
};
} // namespace dex::Snorlax
