#pragma once

#include <array>
#include "dex_peripheral.hpp"

namespace dex::Pikachu
{
inline constexpr std::uint8_t Type = 25;
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
inline constexpr std::uint32_t MinSamplePeriodMs = 1000;
inline constexpr std::uint32_t MaxSamplePeriodMs = 60000;
inline constexpr std::uint32_t MaxPublishPeriodMs = 3600000;
struct InfoData : dex::peripheral::InfoData
{
  std::uint8_t type = Type;
};
struct StatusData : dex::peripheral::StatusData
{
};
#pragma pack(pop)

static_assert(sizeof(BasicChannelSample) == 16);
static_assert(sizeof(BasicSample) == 54);
static_assert(sizeof(MonitoringPublication) == 58);

// Single source of the configuration rule: firmware and hub both call it.
inline bool valid_config(const ConfigData &config)
{
  const auto &m = config.monitoring;
  return m.enabled <= 1 && m.samplePeriodMs >= MinSamplePeriodMs && m.samplePeriodMs <= MaxSamplePeriodMs &&
         m.publishPeriodMs >= m.samplePeriodMs && m.publishPeriodMs <= MaxPublishPeriodMs;
}

// ---- Metadata (see dex_meta.hpp) ------------------------------------------
constexpr auto dex_describe(const BasicChannelSample *)
{
  using Self = BasicChannelSample;
  return meta::fields(DEX_FIELD(current).unit("A").scale(0.01),
                      DEX_FIELD(peakCurrent).unit("A").scale(0.01),
                      DEX_FIELD(power).unit("W"),
                      DEX_FIELD(powerFactor).scale(0.001),
                      DEX_FIELD(energy).unit("Wh").doc("Accumulated during this ESP32 session"));
}

constexpr auto dex_describe(const BasicSample *)
{
  using Self = BasicSample;
  return meta::fields(DEX_FIELD(frequency).unit("Hz").scale(0.01),
                      DEX_FIELD(voltage).unit("V").scale(0.1),
                      DEX_FIELD(peakVoltage).unit("V").scale(0.1),
                      DEX_FIELD(channels).label("Channel"));
}

constexpr auto dex_describe(const ConfigData *)
{
  return meta::fields(meta::base<peripheral::ConfigData>(),
                      meta::at("monitoring.samplePeriodMs").range(MinSamplePeriodMs, MaxSamplePeriodMs),
                      meta::at("monitoring.publishPeriodMs").range(MinSamplePeriodMs, MaxPublishPeriodMs)
                          .describe("Not shorter than the sample period"));
}

// Peripheral traits: everything a host needs to drive this peripheral.
struct Peripheral
{
  static constexpr std::uint8_t type = Type;
  static constexpr const char *name = "Pikachu";
  static constexpr const char *description = "Three-channel power meter (RBAMP)";
  using Status = StatusData;
  using Config = ConfigData;
  using Action = meta::None;
  using Monitoring = MonitoringPublication;
  static bool valid(const Config &config) { return valid_config(config); }
};
} // namespace dex::Pikachu
