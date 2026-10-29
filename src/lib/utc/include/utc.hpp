#pragma once

#include <cstdint>
#include "esp_err.h"

namespace utc
{
// Call once after Wi-Fi initialization. Waits for the first Internet NTP sync.
esp_err_t initialize();

// Unix UTC seconds from the synchronized system clock.
std::uint32_t get();
} // namespace utc
