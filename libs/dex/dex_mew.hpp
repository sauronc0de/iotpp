#pragma once

#include <array>
#include "dex_peripheral.hpp"

namespace dex::Mew
{
inline constexpr std::uint8_t Type = 151;
enum class Action : std::uint8_t
{
  FirmwareCheck = 0
};
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

#pragma pack(push, 1)
struct InfoData : peripheral::InfoData
{
  std::uint8_t type = Type;
};
struct SlotData
{
  char label[17]{};
  char version[33]{};
  SlotState state = SlotState::Empty;
  std::uint8_t running = 0;
  std::uint8_t nextBoot = 0;
};
struct StatusData : peripheral::StatusData
{
  Phase phase = Phase::Idle;
  std::int32_t lastError = 0; // Raw ESP-IDF error, independent of DEX RESULT.
  std::uint32_t downloaded = 0;
  std::uint32_t total = 0;
  std::uint32_t lastCheckUtc = 0;
  char candidate[33]{};
  std::array<SlotData, 2> slots{};
};
struct ConfigData
{
  std::uint32_t intervalSeconds = 21600; // 0 = manual only; maximum 2592000.
};
struct ActionData
{
  Action action = Action::FirmwareCheck;
};
#pragma pack(pop)
static_assert(sizeof(InfoData) == 6);
static_assert(sizeof(SlotData) == 53);
static_assert(sizeof(StatusData) == 166);
static_assert(sizeof(ConfigData) == 4);

inline constexpr std::uint32_t MaxCheckIntervalSeconds = 2592000; // 30 days.
inline bool valid_config(const ConfigData &config) { return config.intervalSeconds <= MaxCheckIntervalSeconds; }

// ---- Metadata (see dex_meta.hpp) ------------------------------------------
constexpr auto dex_describe(const SlotData *)
{
  using Self = SlotData;
  return meta::fields(DEX_FIELD(label), DEX_FIELD(version), DEX_FIELD(state),
                      DEX_FIELD(running).boolean(), DEX_FIELD(nextBoot).boolean());
}

constexpr auto dex_describe(const StatusData *)
{
  using Self = StatusData;
  return meta::fields(meta::base<peripheral::StatusData>(), DEX_FIELD(phase),
                      DEX_FIELD(lastError).label("Last ESP-IDF error"),
                      DEX_FIELD(downloaded).unit("B"), DEX_FIELD(total).unit("B"),
                      DEX_FIELD(lastCheckUtc).label("Last check").unit("s"),
                      DEX_FIELD(candidate), DEX_FIELD(slots).label("Slot"));
}

constexpr auto dex_describe(const ConfigData *)
{
  using Self = ConfigData;
  return meta::fields(DEX_FIELD(intervalSeconds).label("Check interval").unit("s").range(0, MaxCheckIntervalSeconds)
                          .doc("0 = manual checks only"));
}

constexpr auto dex_describe(const ActionData *)
{
  using Self = ActionData;
  return meta::fields(DEX_FIELD(action));
}

struct Peripheral
{
  static constexpr std::uint8_t type = Type;
  static constexpr const char *name = "Mew";
  static constexpr const char *description = "OTA firmware updates";
  using Status = StatusData;
  using Config = ConfigData;
  using Action = ActionData;
  using Monitoring = meta::None;
  static bool valid(const Config &config) { return valid_config(config); }
};
} // namespace dex::Mew
