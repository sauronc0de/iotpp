#pragma once

#include <array>
#include <atomic>
#include "rbamp_reading.hpp"
#include "driver/i2c_master.h"
#include "rbamp.h"
#include "rbamp_energy.h"

namespace rbamp_async
{
struct Config
{
  int port;
  gpio_num_t sda, scl;
  std::uint8_t address;
  int ctModel; // -1 preserves sensor configuration
};

// One owner task and one ISR; no application semaphore, sleep or bus wait.
// All buffers live here until the ISR completes the submitted transfer.
class Meter
{
public:
  esp_err_t initialize(Config config);
  void poll(std::int64_t now);
  bool ready() const { return phase_ == Phase::Idle; }
  bool sample_pending() const { return phase_ == Phase::Sample || complete_; }
  std::uint8_t channels() const { return channels_; }
  bool basic_three_channel() const { return !std_ && channels_ == 3 && voltages_ == 1; }
  esp_err_t error() const { return error_; }
  bool request(std::int64_t now);
  bool take(Reading &sample);

private:
  enum class Phase
  {
    Detect,
    Identity,
    Models,
    Configure,
    Verify,
    Prime,
    Idle,
    Sample,
    Retry
  };
  struct Operation
  {
    std::uint16_t reg;
    std::uint16_t size;
    std::uint8_t *read;
    std::uint8_t value;
    std::uint16_t settleMs;
    bool word;
    bool latch;
  };
  static bool done(i2c_master_dev_handle_t, const i2c_master_event_data_t *, void *context);
  void reset_program(Phase phase);
  void read(std::uint16_t reg, void *destination, std::size_t bytes, bool word);
  void write(std::uint16_t reg, std::uint8_t value, bool word, std::uint16_t settleMs = 0, bool latch = false);
  void basic_channel(std::uint8_t ch, std::uint8_t field, std::uint16_t flat, void *destination, std::size_t bytes = 4);
  void models(Phase next);
  void advance(std::int64_t now);
  void submit(std::int64_t now);
  void fail(esp_err_t error, std::int64_t now);
  void finish_sample(std::int64_t now);
  void decode_counter_energy();
  void decode_basic_energy();

  Config config_{};
  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  std::atomic<unsigned> event_{0};
  std::array<Operation, 128> operations_{};
  std::size_t operationCount_ = 0, operation_ = 0, byte_ = 0, transferBytes_ = 0;
  std::array<i2c_operation_job_t, 7> jobs_{};
  std::array<std::uint8_t, 4> tx_{};
  std::size_t jobCount_ = 0;
  Phase phase_ = Phase::Retry;
  esp_err_t error_ = ESP_OK;
  bool pending_ = false, complete_ = false, std_ = false, resetIssued_ = false;
  bool anchored_ = false, counterAnchored_ = false, primed_ = false;
  unsigned retries_ = 0;
  std::int64_t notBefore_ = 0, submittedAt_ = 0, latchAt_ = 0, previousLatch_ = 0;
  std::uint8_t channels_ = 0, voltages_ = 0, sensorClass_ = 0, map_ = 0;
  std::uint16_t caps_ = 0;
  std::uint32_t firmware_ = 0;
  std::uint32_t lastUptime_ = 0;
  unsigned emptyPeriods_ = 0;
  std::array<std::uint8_t, 10> identity_{};
  std::array<std::uint8_t, 8> metadata_{};
  std::array<std::uint8_t, 7> associations_{};
  std::array<std::array<std::uint8_t, 4>, 7> models_{}, gains_{};
  std::array<float, 7> scales_{};
  std::array<std::array<std::uint8_t, 64>, 7> current_{};
  std::array<std::array<std::uint8_t, 12>, 8> voltage_{};
  std::array<std::uint8_t, 4> frequency_{}, uptime_{};
  std::array<std::array<std::uint8_t, 4>, 7> average_{};
  std::array<std::uint8_t, 52> latch_{};
  std::array<std::array<std::uint8_t, 16>, 7> channelEnergy_{};
  std::array<std::uint8_t, 68> counters_{};
  std::array<std::uint8_t, 40> fineCounters_{};
  std::array<std::uint8_t, 7> configuration_{};
  std::uint8_t valid_ = 0, groupStatus_ = 0;
  rbamp_energy_t energy_{};
  rbamp_std_energy_t previousCounters_{};
  std::array<bool, 7> energyValid_{};
  Reading sample_{};
};
} // namespace rbamp_async
