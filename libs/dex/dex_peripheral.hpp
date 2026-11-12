#pragma once

#include <cstdint>

#include "dex_common.hpp"

namespace dex::peripheral
{

#pragma pack(push, 1)

// Peripheral commands/publications carry the instance ID before their payload.
template <typename T>
struct AddressedData
{
  std::uint8_t peripheralId;
  T data;
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

// ---- Metadata (see dex_meta.hpp) ------------------------------------------
constexpr bool dex_flags(PeripheralCapability) { return true; }

constexpr auto dex_describe(const MonitoringConfigData *)
{
  using Self = MonitoringConfigData;
  return meta::fields(DEX_FIELD(samplePeriodMs).label("Sample period").unit("ms"),
                      DEX_FIELD(publishPeriodMs).label("Publish period").unit("ms"),
                      DEX_FIELD(enabled).boolean());
}

constexpr auto dex_describe(const InfoData *)
{
  using Self = InfoData;
  return meta::fields(DEX_FIELD(id), DEX_FIELD(version), DEX_FIELD(capabilities));
}

constexpr auto dex_describe(const ConfigData *)
{
  using Self = ConfigData;
  return meta::fields(DEX_FIELD(monitoring));
}

} // namespace dex::peripheral
