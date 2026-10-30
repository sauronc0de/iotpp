#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

#include "dex_peripheral.hpp"

namespace dex::device
{
#pragma pack(push, 1)

template <size_t peripheralCount>
struct InfoData
{
  Version firmwareVersion;
  std::array<peripheral::InfoData, peripheralCount> peripherals;
};

template <size_t peripheralCount>
struct StatusData
{
  State state;
  Error errors;
  Notification notifications;
  std::array<peripheral::StatusData, peripheralCount> peripherals;
};

struct ConfigData
{
};

#pragma pack(pop)
} // namespace dex::device
