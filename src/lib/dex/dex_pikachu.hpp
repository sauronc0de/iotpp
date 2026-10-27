#pragma once

#include <array>

#include "dex_peripheral.hpp"

namespace dex::Pikachu
{
#pragma pack(push, 1)

enum class MonitoringFlags : std::uint32_t
{
  None = 0,
  Voltage = 1UL << 0,
  Current = 1UL << 1,
  Power = 1UL << 2,
  Energy = 1UL << 3,
  PowerFactor = 1UL << 4,
  Frequency = 1UL << 5,
  ReactivePower = 1UL << 6,
  PeakVoltage = 1UL << 7,
  PeakCurrent = 1UL << 8,

  All = (1UL << 9) - 1
};

struct ChannelData
{
  float current; // A
  float voltage; // V
  float power;   // W
  double energy; // Wh
};

struct MonitoringData
{
  std::uint32_t timestamp; // Unix UTC seconds
  std::array<ChannelData, 3> channels;
};

struct ConfigData : dex::peripheral::ConfigData
{
  MonitoringFlags fields{MonitoringFlags::All};
};

struct InfoData : dex::peripheral::InfoData
{
  std::uint8_t type = 25;
};

struct StatusData : dex::peripheral::StatusData
{
};

#pragma pack(pop)
} // namespace dex::Pikachu
