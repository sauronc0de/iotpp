#pragma once

#include "esp_err.h"
#include "dex_server.hpp"

esp_err_t start_monitor_rbamp_task();
esp_err_t restore_monitor_rbamp_config();
dex::ProcessResult process_monitor_rbamp_request(const dex::Request &request, std::uint8_t *response, std::size_t capacity);
