#pragma once

#include "esp_err.h"

namespace wifi
{
// Initialize the default station once. Reconnect automatically after disconnects.
esp_err_t initialize(const char *ssid, const char *password);

// Block until the station has an IP address. Call after successful initialization.
void wait_connected();

// Nonblocking; false before initialization or while disconnected.
bool is_connected();
} // namespace wifi
