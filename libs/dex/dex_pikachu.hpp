#pragma once

#include <array>
#include "dex_peripheral.hpp"

namespace dex::Pikachu
{
inline constexpr std::size_t BasicChannelCount = 3;

#pragma pack(push, 1)
struct BasicChannelSample
{
  std::uint16_t current;     // 0.01 A per unit
  std::uint16_t peakCurrent; // 0.01 A per unit
  std::int16_t power;        // 1 W per unit, signed
  std::int16_t powerFactor;  // 0.001 per unit
  double energy;             // Wh accumulated during this ESP32 session, signed
};

struct BasicSample
{
  std::uint16_t frequency;   // 0.01 Hz per unit
  std::uint16_t voltage;     // 0.1 V per unit, shared by all three channels
  std::uint16_t peakVoltage; // 0.1 V per unit
  std::array<BasicChannelSample, BasicChannelCount> channels;
};

using MonitoringPublication = dex::MonitoringPublication<BasicSample>;

struct ConfigData : dex::peripheral::ConfigData
{
};
struct InfoData : dex::peripheral::InfoData
{
  std::uint8_t type = 25;
};
struct StatusData : dex::peripheral::StatusData
{
};
#pragma pack(pop)

static_assert(sizeof(BasicChannelSample) == 16);
static_assert(sizeof(BasicSample) == 54);
static_assert(sizeof(MonitoringPublication) == 58);
} // namespace dex::Pikachu
