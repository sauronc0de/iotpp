#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace config_store
{
struct PeripheralKey
{
  std::uint16_t type;
  std::uint8_t instance;
};

enum class Result
{
  Ok,
  NotFound,
  SizeMismatch,
  StorageFailed,
  NotReady
};

// Initialize once before tasks start. Never erases existing NVS data.
Result initialize();

namespace detail
{
Result read_blob(PeripheralKey key, void *data, std::size_t size);
Result write_blob(PeripheralKey key, const void *data, std::size_t size);
bool begin_transaction();
void end_transaction();
} // namespace detail

// Keep the chip awake until a configuration operation (including its reply)
// finishes. Transactions may nest; no flash operation runs in a critical section.
class Transaction
{
public:
  Transaction() : active_(detail::begin_transaction()) {}
  ~Transaction() { if(active_) detail::end_transaction(); }
  Transaction(const Transaction &) = delete;
  Transaction &operator=(const Transaction &) = delete;
  explicit operator bool() const { return active_; }

private:
  bool active_;
};

// Once committed, later transactions fail rather than acknowledging a change
// while the chip is already entering Deep Sleep. Reset clears this gate.
bool try_begin_sleep();

template <typename T>
Result load_config(PeripheralKey key, T &config)
{
  static_assert(std::is_trivially_copyable_v<T>, "ConfigData must be a pointer-free binary value");
  T candidate{};
  const auto result = detail::read_blob(key, &candidate, sizeof(T));
  if(result == Result::Ok)
    std::memcpy(&config, &candidate, sizeof(T));
  return result; // Missing/incompatible/failed reads leave defaults untouched.
}

template <typename T>
Result save_config(PeripheralKey key, const T &config)
{
  static_assert(std::is_trivially_copyable_v<T>, "ConfigData must be a pointer-free binary value");
  Transaction transaction;
  if(!transaction)
    return Result::NotReady;
  T previous{};
  const auto result = load_config(key, previous);
  if(result == Result::Ok && std::memcmp(&previous, &config, sizeof(T)) == 0)
    return Result::Ok;
  if(result != Result::Ok && result != Result::NotFound)
    return result;
  return detail::write_blob(key, &config, sizeof(T));
}
} // namespace config_store
