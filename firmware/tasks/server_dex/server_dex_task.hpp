#pragma once

#include "esp_err.h"

// Call once, after successful Wi-Fi initialization.
esp_err_t start_server_dex_task();
