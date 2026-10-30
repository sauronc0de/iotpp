#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rbamp_async
{
// Decoded SI readings are independent of the compact Pikachu wire/storage
// format. Unavailable or rejected quantities are NaN until validation.
inline constexpr std::size_t MaxChannels = 7;
enum class Line
{
  Basic,
  Standard
};

struct ChannelReading
{
  float current, voltage, power;
  double energy;
  float powerFactor, reactivePower, apparentPower;
  float peakVoltage, peakCurrent, voltageThd, currentThd;
};

struct Reading
{
  Line line;
  std::uint8_t channelCount, voltageCount;
  float frequency;
  std::array<ChannelReading, MaxChannels> channels;
};
} // namespace rbamp_async
