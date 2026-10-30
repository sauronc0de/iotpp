#pragma once

#include <cstdint>
#include "esp_err.h"

namespace utc
{
// Start shared SNTP without waiting for network/time; safe to call repeatedly.
esp_err_t start();
// Call once after Wi-Fi initialization. Waits for the first Internet NTP sync.
esp_err_t initialize();

// Unix UTC seconds from the synchronized system clock.
std::uint32_t get();
} // namespace utc
