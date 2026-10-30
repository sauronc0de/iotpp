#pragma once

#include <cstdint>
#include "esp_err.h"

namespace ota
{
inline constexpr std::uint32_t DefaultIntervalSeconds = 6 * 60 * 60;
enum class Phase : std::uint8_t
{
  Idle,
  WaitingNetwork,
  Checking,
  Downloading,
  Verifying,
  RebootPending,
  PendingValidation,
  Error
};
enum class SlotState : std::uint8_t
{
  Empty,
  Unknown,
  New,
  PendingVerify,
  Valid,
  Invalid,
  Aborted
};

struct Config
{
  std::uint32_t intervalSeconds = DefaultIntervalSeconds; // Zero disables automatic checks.
};
struct Slot
{
  char label[17]{};
  char version[33]{};
  SlotState state = SlotState::Empty;
  bool running = false;
  bool nextBoot = false;
};
struct Status
{
  Phase phase = Phase::Idle;
  std::int32_t lastError = ESP_OK;
  std::uint32_t downloaded = 0;
  std::uint32_t total = 0;
  std::uint32_t lastCheckUtc = 0;
  char candidate[33]{};
  Slot slots[2]{};
};

// Restore before starting application tasks; start after shared Wi-Fi initialization.
esp_err_t initialize();
esp_err_t start();
esp_err_t firmware_check(); // Nonblocking; accepted means queued, not installed.
esp_err_t configure(Config config);
Config configuration();
Status status();
} // namespace ota
