#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "dex.hpp"
#include "rbamp_reading.hpp"

namespace monitoring
{
inline constexpr std::int64_t HourUs = 3600000000LL;
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

inline bool make_basic_sample(const rbamp_async::Reading &reading, std::uint32_t utc, dex::Pikachu::BasicSample &sample)
{
  if(utc == 0 || reading.line != rbamp_async::Line::Basic ||
     reading.channelCount != dex::Pikachu::BasicChannelCount || reading.voltageCount != 1)
    return false;
  dex::Pikachu::BasicSample candidate{};
  candidate.timestamp = utc;
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
  sample = candidate; // Publish only after validating the entire required sample.
  return true;
}

#pragma pack(push, 1)
struct BasicHistoryEntry
{
  std::uint32_t capturedAt; // Monotonic seconds, never transmitted.
  dex::Pikachu::BasicSample sample;
};
#pragma pack(pop)
static_assert(sizeof(BasicHistoryEntry) == 62);

struct SampleId
{
  std::uint32_t session;
  std::uint32_t sequence;
};

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
  void initialize(void *storage, std::size_t slots, std::uint32_t session)
  {
    storage_ = static_cast<BasicHistoryEntry *>(storage);
    slots_ = slots;
    head_ = count_ = 0;
    session_ = session;
    headSequence_ = dropped_ = 0;
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
  void push(const dex::Pikachu::BasicSample &sample, std::uint32_t capturedAt)
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
  const dex::Pikachu::BasicSample *front() const
  {
    return count_ == 0 ? nullptr : &storage_[head_].sample;
  }
  SampleId front_id() const { return {session_, headSequence_}; }
  bool confirm(SampleId id)
  {
    if(count_ == 0)
      return false;
    if(session_ != id.session || headSequence_ != id.sequence)
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
  std::uint32_t session_ = 0, headSequence_ = 0, dropped_ = 0;
};

// NTP v4 client validation/time conversion; transport and retry scheduling are
// asynchronous and owned by the monitoring task. No system clock semaphore.
class UtcClock
{
public:
  std::array<std::uint8_t, 48> request(std::uint64_t nonce, std::int64_t now)
  {
    nonce_ = nonce;
    sentAt_ = now;
    pending_ = true;
    std::array<std::uint8_t, 48> packet{};
    packet[0] = 0x23; // LI=0, version=4, mode=client
    for(unsigned i = 0; i < 8; ++i)
      packet[40 + i] = static_cast<std::uint8_t>(nonce >> (56 - i * 8));
    return packet;
  }
  bool accept(dex::Bytes response, std::int64_t now)
  {
    if(!pending_ || response.size < 48 || now < sentAt_ || now - sentAt_ > 5000000)
      return false;
    const auto *p = response.data;
    if((p[0] >> 6) == 3 || ((p[0] >> 3) & 7) < 3 || ((p[0] >> 3) & 7) > 4 || (p[0] & 7) != 4 || p[1] == 0 || p[1] > 15)
      return false;
    std::uint64_t originate = 0;
    for(unsigned i = 0; i < 8; ++i)
      originate = (originate << 8) | p[24 + i];
    if(originate != nonce_)
      return false;
    const auto seconds = be32(p + 40);
    const auto fraction = be32(p + 44);
    if(seconds == 0 && fraction == 0)
      return false;
    // Era 0 before the 2036 NTP rollover, era 1 afterwards (valid until 2106).
    const std::uint64_t ntp = seconds < 2208988800UL ? (1ULL << 32) + seconds : seconds;
    const auto utc = ntp - 2208988800ULL;
    if(utc < 1704067200ULL || utc > std::numeric_limits<std::uint32_t>::max())
      return false;
    anchorUtcUs_ = utc * 1000000ULL + (static_cast<std::uint64_t>(fraction) * 1000000ULL >> 32) + (now - sentAt_) / 2;
    anchorMonoUs_ = now;
    valid_ = true;
    pending_ = false;
    return true;
  }
  bool valid() const { return valid_; }
  std::uint32_t seconds(std::int64_t now) const
  {
    const auto utc = (anchorUtcUs_ + now - anchorMonoUs_) / 1000000;
    return valid_ && utc >= 0 && utc <= std::numeric_limits<std::uint32_t>::max() ? static_cast<std::uint32_t>(utc) : 0;
  }

private:
  static std::uint32_t be32(const std::uint8_t *p)
  {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
  }
  std::uint64_t nonce_ = 0;
  std::int64_t sentAt_ = 0, anchorUtcUs_ = 0, anchorMonoUs_ = 0;
  bool pending_ = false, valid_ = false;
};
} // namespace monitoring
