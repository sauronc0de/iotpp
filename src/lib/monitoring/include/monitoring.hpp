#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "dex.hpp"
#include "rbamp_reading.hpp"

namespace monitoring
{
inline constexpr std::uint32_t HourSeconds = 3600;

// Round to the nearest storage unit (half away from zero). Check before the
// integer cast: invalid/overflowing values must never wrap or saturate.
template <typename Integer>
bool quantize(float value, double unitsPerValue, Integer &encoded)
{
  if(!std::isfinite(value) || (std::numeric_limits<Integer>::is_signed == false && value < 0))
    return false;
  const auto rounded = std::round(static_cast<double>(value) * unitsPerValue);
  if(!std::isfinite(rounded) || rounded < std::numeric_limits<Integer>::lowest() || rounded > std::numeric_limits<Integer>::max())
    return false;
  encoded = static_cast<Integer>(rounded);
  return true;
}

inline bool make_basic_sample(const rbamp_async::Reading &reading, std::uint32_t utc, dex::Pikachu::MonitoringPublication &publication)
{
  if(utc == 0 || reading.line != rbamp_async::Line::Basic ||
     reading.channelCount != dex::Pikachu::BasicChannelCount || reading.voltageCount != 1)
    return false;
  dex::Pikachu::BasicSample candidate{};
  if(!quantize(reading.frequency, 100, candidate.frequency) ||
     !quantize(reading.channels[0].voltage, 10, candidate.voltage) ||
     !quantize(reading.channels[0].peakVoltage, 10, candidate.peakVoltage))
    return false;
  for(std::size_t ch = 0; ch < candidate.channels.size(); ++ch)
  {
    const auto &in = reading.channels[ch];
    auto &out = candidate.channels[ch];
    if(!quantize(in.current, 100, out.current) || !quantize(in.peakCurrent, 100, out.peakCurrent) ||
       !quantize(in.power, 1, out.power) || !quantize(in.powerFactor, 1000, out.powerFactor) || !std::isfinite(in.energy))
      return false;
    out.energy = in.energy;
  }
  publication = {utc, candidate}; // Publish only after validating the entire required sample.
  return true;
}

#pragma pack(push, 1)
struct BasicHistoryEntry
{
  std::uint32_t capturedAt; // Monotonic seconds, never transmitted.
  dex::Pikachu::MonitoringPublication sample;
};
#pragma pack(pop)
static_assert(sizeof(BasicHistoryEntry) == 62);

// Single-owner fixed-size FIFO. Storage is allocated once by the application,
// in PSRAM when available. Neither the ISR nor network callbacks access it.
class History
{
public:
  static constexpr std::size_t capacity(std::uint32_t samplePeriodMs)
  {
    return samplePeriodMs == 0 ? 0 : (3600000ULL + samplePeriodMs - 1) / samplePeriodMs + 1;
  }
  static constexpr std::size_t required_bytes(std::uint32_t period)
  {
    return capacity(period) * sizeof(BasicHistoryEntry);
  }
  void initialize(void *storage, std::size_t slots)
  {
    storage_ = static_cast<BasicHistoryEntry *>(storage);
    slots_ = slots;
    head_ = count_ = 0;
    headSequence_ = dropped_ = 0;
  }
  // Move to a larger allocation without losing pending samples or their IDs.
  // Only the owning task calls this; network callbacks never reference entries.
  bool relocate(void *storage, std::size_t slots)
  {
    if(storage == nullptr || slots < count_)
      return false;
    auto *destination = static_cast<BasicHistoryEntry *>(storage);
    for(std::size_t i = 0; i < count_; ++i)
      destination[i] = storage_[(head_ + i) % slots_];
    storage_ = destination;
    slots_ = slots;
    head_ = 0;
    return true;
  }
  std::size_t expire(std::uint32_t now)
  {
    std::size_t expired = 0;
    while(count_ != 0)
    {
      // Unsigned subtraction also handles wrap of the 32-bit monotonic clock.
      if(static_cast<std::uint32_t>(now - storage_[head_].capturedAt) < HourSeconds)
        break;
      pop();
      ++expired;
    }
    dropped_ += expired;
    return expired;
  }
  void push(const dex::Pikachu::MonitoringPublication &sample, std::uint32_t capturedAt)
  {
    expire(capturedAt);
    if(count_ == slots_)
    {
      pop();
      ++dropped_;
    }
    storage_[(head_ + count_) % slots_] = {capturedAt, sample};
    ++count_;
  }
  const dex::Pikachu::MonitoringPublication *front() const
  {
    return count_ == 0 ? nullptr : &storage_[head_].sample;
  }
  // Local FIFO token only; never transmitted.
  std::uint32_t front_id() const { return headSequence_; }
  bool consume(std::uint32_t id)
  {
    if(count_ == 0)
      return false;
    if(headSequence_ != id)
      return false;
    pop();
    return true;
  }
  std::size_t size() const { return count_; }
  std::uint32_t dropped() const { return dropped_; }

private:
  void pop()
  {
    head_ = (head_ + 1) % slots_;
    --count_;
    ++headSequence_;
  }
  BasicHistoryEntry *storage_ = nullptr;
  std::size_t slots_ = 0, head_ = 0, count_ = 0;
  std::uint32_t headSequence_ = 0, dropped_ = 0;
};

} // namespace monitoring
