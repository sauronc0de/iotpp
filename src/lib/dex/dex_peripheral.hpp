#pragma once

#include <cstdint>

#include "dex_common.hpp"

namespace dex::peripheral
{

#pragma pack(push, 1)

enum class PeripheralCommandId : std::uint8_t
{
  Status = static_cast<std::uint8_t>(CommandId::PeripheralCmd), //* Return peripheral Status
  Config,                                                       //* Set/Get peripheral configuration
  Action,                                                       //* Perform peripheral action
  Monitoring,                                                   //* Get/Publish monitoring data
  Pause,                                                        //* Stop peripheral operation (>151 for broadcast)
  Resume,                                                       //* Resume peripheral operation (>151 for broadcast)
};

enum class PeripheralCapability : std::uint8_t
{
  Monitoring = 1 << 0,
  Events = 1 << 1,
  Actions = 1 << 2,
  Config = 1 << 3
};

struct MonitoringConfigData
{
  std::uint32_t samplePeriodMs = {1000};
  std::uint32_t publishPeriodMs = {10000};
  std::uint8_t enabled = {1}; // 0 = disabled, 1 = enabled
};

struct InfoData
{
  std::uint8_t id;
  Version version;
  PeripheralCapability capabilities;
};

struct StatusData : dex::StatusData
{
};

struct ConfigData
{
  dex::peripheral::MonitoringConfigData monitoring;
};

#pragma pack(pop)

} // namespace dex::peripheral
