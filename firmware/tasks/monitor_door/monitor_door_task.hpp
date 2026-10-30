#pragma once

#include "esp_err.h"
#include "dex_server.hpp"

esp_err_t start_monitor_door_task();
esp_err_t restore_monitor_door_config();
dex::ProcessResult process_monitor_door_request(const dex::Request &request, std::uint8_t *response, std::size_t capacity);
