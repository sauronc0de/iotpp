#pragma once

#include "dex_peripheral.hpp"

namespace dex::Snorlax
{
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
  std::uint8_t type = 143;
};
struct StatusData : dex::peripheral::StatusData
{
  State doorState = State::Unknown;
};
#pragma pack(pop)
static_assert(sizeof(MonitoringPublication) == 5);
} // namespace dex::Snorlax
