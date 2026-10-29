#include "rbamp_async.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "esp_timer.h"
#include "rbamp_std_decode.h"
#include "sdkconfig.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

namespace rbamp_async
{
namespace
{
static_assert(MaxChannels == RBAMP_MAX_CHANNELS);
constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
float le_float(const std::uint8_t *p)
{
  const auto bits = rbampi_le_u32(p);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
void quantity(float value, float &destination, float bound)
{
  if(std::isfinite(value) && std::abs(value) <= bound)
    destination = value;
}
} // namespace

esp_err_t Meter::initialize(Config config)
{
  config_ = config;
  rbamp_energy_init(&energy_);
  scales_.fill(1.0f);
  i2c_master_bus_config_t bus{};
  bus.i2c_port = config.port;
  bus.sda_io_num = config.sda;
  bus.scl_io_num = config.scl;
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.glitch_ignore_cnt = 7;
  bus.trans_queue_depth = 1;
  bus.flags.enable_internal_pullup = true;
  auto error = i2c_new_master_bus(&bus, &bus_);
  if(error != ESP_OK)
    return error;
  i2c_device_config_t device{};
  device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  // Explicit addresses let the same engine handle BASIC's one-byte register
  // transactions and STD's 16-bit pointers without extra device handles.
  device.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
  device.scl_speed_hz = CONFIG_RBAMP_DEFAULT_I2C_FREQ_HZ;
  device.scl_wait_us = std::min<unsigned>(CONFIG_RBAMP_I2C_SCL_WAIT_US, 13000);
  error = i2c_master_bus_add_device(bus_, &device, &device_);
  if(error != ESP_OK)
    return error;
  i2c_master_event_callbacks_t callbacks{};
  callbacks.on_trans_done = done;
  error = i2c_master_register_event_callbacks(device_, &callbacks, this);
  if(error != ESP_OK)
    return error;
  reset_program(Phase::Detect);
  read(RBAMP_STD_REG_ID, identity_.data(), identity_.size(), true);
  return ESP_OK;
}

bool IRAM_ATTR Meter::done(i2c_master_dev_handle_t, const i2c_master_event_data_t *event, void *context)
{
  static_cast<Meter *>(context)->event_.store(static_cast<unsigned>(event->event) + 1, std::memory_order_release);
  return false;
}

void Meter::reset_program(Phase phase)
{
  phase_ = phase;
  operationCount_ = operation_ = byte_ = 0;
  retries_ = 0;
}

void Meter::read(std::uint16_t reg, void *destination, std::size_t bytes, bool word)
{
  operations_[operationCount_++] = {reg, static_cast<std::uint16_t>(bytes), static_cast<std::uint8_t *>(destination), 0, 0, word, false};
}

void Meter::write(std::uint16_t reg, std::uint8_t value, bool word, std::uint16_t settleMs, bool latch)
{
  operations_[operationCount_++] = {reg, 1, nullptr, value, settleMs, word, latch};
}

void Meter::basic_channel(std::uint8_t ch, std::uint8_t field, std::uint16_t flat, void *destination, std::size_t bytes)
{
  if(ch < 3)
    read(flat, destination, bytes, false);
  else
  {
    write(RBAMP_V2_REG_CHANNEL_SELECT, (field << 4) | ch, false);
    read(RBAMP_V2_REG_CHANNEL_DATA, destination, bytes, false);
  }
}

void Meter::models(Phase next)
{
  reset_program(next);
  for(std::uint8_t ch = 0; ch < channels_; ++ch)
  {
    if(std_)
      read(RBAMP_STD_CHMAP_MODEL_BASE + ch, models_[ch].data(), 1, true);
    else
    {
      basic_channel(ch, RBAMP_V2_CHFIELD_CT_MODEL, RBAMP_V2_REG_CT_MODEL_CH0 + ch, models_[ch].data(), 1);
      basic_channel(ch, RBAMPI_CHFIELD_GAIN, RBAMP_V2_REG_V03_I0_GAIN + ch * 4, gains_[ch].data());
    }
  }
}

void Meter::fail(esp_err_t error, std::int64_t now)
{
  error_ = error;
  phase_ = Phase::Retry;
  notBefore_ = now + 5000000;
  anchored_ = false;
}

void Meter::poll(std::int64_t now)
{
  if(pending_)
  {
    const auto event = event_.exchange(0, std::memory_order_acquire);
    if(event == 0)
    {
      // Never recycle buffers while hardware may still write into them. A
      // missing ISR faults acquisition only; UDP and FIFO continue running.
      if(now - submittedAt_ > 2000000)
        error_ = ESP_ERR_TIMEOUT;
      return;
    }
    pending_ = false;
    const auto &op = operations_[operation_];
    if(event != static_cast<unsigned>(I2C_EVENT_DONE) + 1)
    {
      if(op.read != nullptr && ++retries_ < CONFIG_RBAMP_NACK_RETRY_ATTEMPTS)
      {
        notBefore_ = now + CONFIG_RBAMP_NACK_RETRY_GAP_MS * 1000LL;
        return;
      }
      fail(event == static_cast<unsigned>(I2C_EVENT_NACK) + 1 ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT, now);
      return;
    }
    retries_ = 0;
    byte_ += transferBytes_;
    if(byte_ >= op.size)
    {
      notBefore_ = now + op.settleMs * 1000LL;
      byte_ = 0;
      ++operation_;
    }
  }
  if(now < notBefore_)
    return;
  if(phase_ == Phase::Retry)
  {
    reset_program(Phase::Detect);
    read(RBAMP_STD_REG_ID, identity_.data(), identity_.size(), true);
  }
  if(phase_ == Phase::Idle || complete_)
    return;
  if(operation_ == operationCount_)
    advance(now);
  if(phase_ != Phase::Idle && phase_ != Phase::Retry && operation_ < operationCount_)
    submit(now);
}

void Meter::submit(std::int64_t now)
{
  const auto &op = operations_[operation_];
  jobCount_ = 0;
  const auto start = [this]() { jobs_[jobCount_++] = {I2C_MASTER_CMD_START, {}}; };
  const auto stop = [this]() { jobs_[jobCount_++] = {I2C_MASTER_CMD_STOP, {}}; };
  const auto out = [this](const std::uint8_t *data, std::size_t size) {
    auto &job = jobs_[jobCount_++];
    job = {};
    job.command = I2C_MASTER_CMD_WRITE;
    job.write = {true, data, size};
  };
  const auto in = [this](std::uint8_t *data, std::size_t size, i2c_ack_value_t ack) {
    auto &job = jobs_[jobCount_++];
    job = {};
    job.command = I2C_MASTER_CMD_READ;
    job.read = {ack, data, size};
  };
  transferBytes_ = op.read != nullptr && op.word ? op.size : 1;
  tx_[0] = config_.address << 1;
  const auto reg = op.reg + (op.word ? 0 : byte_);
  const unsigned pointerSize = op.word ? 2 : 1;
  tx_[1] = op.word ? reg >> 8 : reg;
  if(op.word)
    tx_[2] = reg;
  start();
  if(op.read == nullptr)
  {
    tx_[1 + pointerSize] = op.value;
    out(tx_.data(), 2 + pointerSize);
  }
  else
  {
    out(tx_.data(), 1 + pointerSize);
    start();
    tx_[3] = (config_.address << 1) | 1;
    out(&tx_[3], 1);
    if(transferBytes_ > 1)
      in(op.read + byte_, transferBytes_ - 1, I2C_ACK_VAL);
    in(op.read + byte_ + transferBytes_ - 1, 1, I2C_NACK_VAL);
  }
  stop();
  if(op.latch)
    latchAt_ = now;
  pending_ = true;
  submittedAt_ = now;
  // Callback registration selects the asynchronous path. Only one transfer
  // is ever in flight, so no driver transfer queue can fill or block.
  const auto error = i2c_master_execute_defined_operations(device_, jobs_.data(), jobCount_, 0);
  if(error != ESP_OK)
  {
    pending_ = false;
    fail(error, now);
  }
}

void Meter::advance(std::int64_t now)
{
  switch(phase_)
  {
  case Phase::Detect:
    resetIssued_ = false;
    std_ = identity_[0] == 'S' && identity_[1] == 'T';
    reset_program(Phase::Identity);
    if(std_)
    {
      map_ = identity_[RBAMP_STD_REG_VERSION];
      caps_ = rbampi_le_u16(identity_.data() + RBAMP_STD_REG_CAPABILITIES);
      channels_ = identity_[RBAMP_STD_REG_N_CURRENT];
      voltages_ = identity_[RBAMP_STD_REG_N_VOLTAGE];
      if(map_ < 0x30 || !(caps_ & RBAMP_STD_CAP_ADDR16) || channels_ == 0 || channels_ > 7 || voltages_ > 8)
        return fail(ESP_ERR_NOT_SUPPORTED, now);
      read(RBAMP_STD_REG_CFG_CLASS, configuration_.data(), configuration_.size(), true);
      read(RBAMP_STD_CHMAP_ASSOC_BASE, associations_.data(), channels_, true);
      if(map_ >= 0x35)
        read(RBAMP_STD_REG_FW_VER, metadata_.data(), 4, true);
    }
    else
    {
      read(RBAMP_V2_REG_VERSION, &metadata_[0], 1, false);
      read(RBAMP_V2_REG_PRODUCT_ID, &metadata_[1], 1, false);
      read(RBAMP_V2_REG_HW_VARIANT, &metadata_[2], 1, false);
      read(RBAMP_V2_REG_CAPABILITY, &metadata_[3], 2, false);
      read(RBAMP_V2_REG_SENSOR_CLASS, &sensorClass_, 1, false);
    }
    break;
  case Phase::Identity:
    if(std_)
    {
      sensorClass_ = configuration_[0];
      firmware_ = map_ >= 0x35 ? rbampi_le_u32(metadata_.data()) : 0;
    }
    else
    {
      bool voltage = false;
      if(metadata_[0] == 0 || metadata_[0] == 0xff || metadata_[1] != 1 ||
         !rbampi_basic_variant_shape(metadata_[2], &channels_, &voltage))
        return fail(ESP_ERR_NOT_SUPPORTED, now);
      voltages_ = voltage ? 1 : 0;
      caps_ = rbampi_le_u16(&metadata_[3]);
    }
    models(Phase::Models);
    break;
  case Phase::Models: {
    if(sensorClass_ == RBAMP_SENSOR_UNSET && config_.ctModel < 0)
      return fail(ESP_ERR_INVALID_STATE, now);
    reset_program(Phase::Configure);
    if(config_.ctModel >= 0)
    {
      const auto selected = static_cast<std::uint8_t>(config_.ctModel);
      if(rbampi_sensor_class_paired(sensorClass_) || rbamp_sensor_model_lookup(RBAMP_SENSOR_SCT013, selected) == nullptr)
        return fail(ESP_ERR_INVALID_ARG, now);
      if(sensorClass_ != RBAMP_SENSOR_SCT013)
        write(std_ ? RBAMP_STD_REG_CFG_CLASS : RBAMP_V2_REG_SENSOR_CLASS, RBAMP_SENSOR_SCT013, std_);
      for(std::uint8_t ch = 0; ch < channels_; ++ch)
      {
        if(models_[ch][0] == selected && sensorClass_ == RBAMP_SENSOR_SCT013)
          continue;
        if(std_)
        {
          if(!(caps_ & RBAMP_STD_CAP_CHMAP_PERSIST))
            return fail(ESP_ERR_NOT_SUPPORTED, now);
          write(RBAMP_STD_CHMAP_MODEL_BASE + ch, selected, true);
          if(ch == 0)
            write(RBAMP_STD_REG_CFG_CT_MODEL, selected, true);
        }
        else
        {
          if(ch >= 3)
            return fail(ESP_ERR_NOT_SUPPORTED, now); // Senior binding needs its extended provisioning protocol.
          write(RBAMP_V2_REG_CT_MODEL, selected, false);
          write(RBAMP_V2_REG_COMMAND, RBAMP_V2_CMD_SET_CT_MODEL_CH0 + ch, false, RBAMP_V2_SETTLE_MS_SET_CT_MODEL_CH0);
        }
      }
      if(operationCount_ != 0)
      {
        if(!std_ && !(caps_ & RBAMP_V2_CAP_SAVE_USER_CONFIG))
          return fail(ESP_ERR_NOT_SUPPORTED, now);
        write(std_ ? RBAMP_STD_REG_COMMAND : RBAMP_V2_REG_COMMAND,
              std_ ? RBAMP_STD_CMD_SAVE_CONFIG : RBAMP_V2_CMD_SAVE_USER_CONFIG, std_, 700);
        read(std_ ? RBAMP_STD_REG_CFG_CLASS : RBAMP_V2_REG_SENSOR_CLASS, &sensorClass_, 1, std_);
      }
    }
    break;
  }
  case Phase::Configure:
    models(Phase::Verify);
    break;
  case Phase::Verify: {
    bool reset = false;
    for(std::uint8_t ch = 0; ch < channels_; ++ch)
    {
      if(config_.ctModel >= 0 && (sensorClass_ != RBAMP_SENSOR_SCT013 || models_[ch][0] != config_.ctModel))
        return fail(ESP_ERR_INVALID_RESPONSE, now);
      if(sensorClass_ == RBAMP_SENSOR_SCT013 && rbamp_sensor_model_lookup(sensorClass_, models_[ch][0]) == nullptr)
        return fail(ESP_ERR_INVALID_STATE, now);
      scales_[ch] = 1;
#if CONFIG_RBAMP_SCT_RATING_CORRECTION
      if(!std_)
      {
        bool needed = false;
        rbampi_sct_decide(caps_, sensorClass_, models_[ch][0], le_float(gains_[ch].data()), resetIssued_, &needed, &scales_[ch]);
        reset |= needed;
      }
#endif
    }
    if(reset && !resetIssued_)
    {
      models(Phase::Verify);
      // Insert the reset BEFORE the re-read program.
      std::move_backward(operations_.begin(), operations_.begin() + operationCount_, operations_.begin() + operationCount_ + 1);
      operations_[0] = {RBAMP_V2_REG_COMMAND, 1, nullptr, RBAMP_V2_CMD_RESET, RBAMP_V2_SETTLE_MS_RESET, false, false};
      ++operationCount_;
      resetIssued_ = true;
      break;
    }
    reset_program(Phase::Prime);
    if(!std_)
      write(RBAMP_V2_REG_COMMAND, RBAMP_V2_CMD_LATCH_PERIOD, false, RBAMP_V2_SETTLE_MS_LATCH_PERIOD, true);
    else if(caps_ & RBAMP_STD_CAP_MULTI_LATCH)
    {
      write(RBAMP_STD_REG_PERIOD_ARM, 1, true);
      write(RBAMP_STD_REG_LATCH_BANK, 0, true);
      write(RBAMP_STD_REG_COMMAND, RBAMP_STD_CMD_LATCH_PERIOD, true, 300, true);
    }
    break;
  }
  case Phase::Prime:
    previousLatch_ = latchAt_;
    anchored_ = true;
    primed_ = true;
    error_ = ESP_OK;
    reset_program(Phase::Idle);
    break;
  case Phase::Sample:
    finish_sample(now);
    if(phase_ == Phase::Retry)
      break;
    complete_ = true;
    reset_program(Phase::Idle);
    break;
  default:
    break;
  }
}

bool Meter::request(std::int64_t now)
{
  if(!ready() || complete_ || !primed_)
    return false;
  sample_ = {};
  sample_.line = std_ ? Line::Standard : Line::Basic;
  sample_.channelCount = channels_;
  sample_.voltageCount = voltages_;
  sample_.frequency = NaN;
  energyValid_.fill(false);
  for(auto &channel : sample_.channels)
    channel = {NaN, NaN, NaN, NaN, NaN, NaN, NaN, NaN, NaN, NaN, NaN};
  reset_program(Phase::Sample);
  if(std_)
  {
    if(caps_ & RBAMP_STD_CAP_MULTI_LATCH)
    {
      write(RBAMP_STD_REG_COMMAND, RBAMP_STD_CMD_LATCH_PERIOD, true, 300, true);
      read(RBAMP_STD_REG_PD_VALID, &valid_, 1, true);
      read(RBAMP_STD_LATCH_BASE, latch_.data(), latch_.size(), true);
      for(std::uint8_t ch = 0; ch < channels_; ++ch)
        read(RBAMP_STD_LATCH_BASE + RBAMP_STD_LATCH_CHE_BASE + ch * RBAMP_STD_LATCH_CHE_STRIDE, channelEnergy_[ch].data(), 16, true);
    }
    else if(caps_ & RBAMP_STD_CAP_ONDEVICE_ENERGY)
    {
      read(RBAMP_STD_ENERGY_BASE, counters_.data(), counters_.size(), true);
      if(caps_ & RBAMP_STD_CAP_ENERGY_UWH)
        read(RBAMP_STD_ENERGY_BASE + RBAMP_STD_ENERGY_E_ACT_IMP_UWH, fineCounters_.data(), fineCounters_.size(), true);
      if(map_ >= 0x36)
      {
        read(RBAMP_STD_REG_LTG_STATUS, &groupStatus_, 1, true);
        for(std::uint8_t ch = 0; ch < channels_; ++ch)
          read((caps_ & RBAMP_STD_CAP_ENERGY_UWH ? RBAMP_STD_GROUP_ENERGY_UWH_BASE : RBAMP_STD_GROUP_ENERGY_MWH_BASE) + ch * 16, channelEnergy_[ch].data(), 16, true);
      }
    }
    read(RBAMP_STD_REG_FREQ, frequency_.data(), 2, true);
    read(RBAMP_STD_ENERGY_BASE + RBAMP_STD_ENERGY_UPTIME, uptime_.data(), 4, true);
    for(std::uint8_t v = 0; v < voltages_; ++v)
      read(RBAMP_STD_VOLTAGE_BASE + v * RBAMP_STD_VOLTAGE_STRIDE, voltage_[v].data(), 12, true);
    for(std::uint8_t ch = 0; ch < channels_; ++ch)
      read(RBAMP_STD_CURRENT_BASE + ch * RBAMP_STD_CURRENT_STRIDE, current_[ch].data(), 48, true);
  }
  else
  {
    write(RBAMP_V2_REG_COMMAND, RBAMP_V2_CMD_LATCH_PERIOD, false, RBAMP_V2_SETTLE_MS_LATCH_PERIOD, true);
    read(RBAMP_V2_REG_V03_PERIOD_VALID, &valid_, 1, false);
    read(RBAMP_V2_REG_UPTIME_S, uptime_.data(), 4, false);
    for(std::uint8_t ch = 0; ch < channels_; ++ch)
    {
      const auto avgReg = ch == 0 ? RBAMP_V2_REG_V03_PERIOD_AVG_P : RBAMP_V2_REG_V03_PERIOD_AVG_P_CH1 + (ch - 1) * 4;
      basic_channel(ch, RBAMP_V2_CHFIELD_PERIOD_AVG_P, avgReg, average_[ch].data());
      basic_channel(ch, RBAMP_V2_CHFIELD_I_RMS, RBAMP_V2_REG_V03_I0_RMS + ch * 4, &current_[ch][0]);
      basic_channel(ch, RBAMP_V2_CHFIELD_I_PEAK, RBAMP_V2_REG_V03_I0_PEAK + ch * 4, &current_[ch][4]);
      if(voltages_ != 0)
      {
        basic_channel(ch, RBAMP_V2_CHFIELD_P_REAL, RBAMP_V2_REG_V03_P0_REAL + ch * 4, &current_[ch][8]);
        basic_channel(ch, RBAMP_V2_CHFIELD_PF, RBAMP_V2_REG_V03_PF0 + ch * 4, &current_[ch][12]);
      }
    }
    if(voltages_ != 0)
    {
      read(RBAMP_V2_REG_V03_U_RMS, &voltage_[0][0], 4, false);
      read(RBAMP_V2_REG_V03_U_PEAK, &voltage_[0][4], 4, false);
      read(RBAMP_V2_REG_AC_FREQ, frequency_.data(), 1, false);
    }
  }
  latchAt_ = now;
  return true;
}

void Meter::decode_basic_energy()
{
  if((valid_ & 1) == 0 || !anchored_)
    return;
  std::array<float, 7> power{};
  bool valid = true;
  for(std::uint8_t ch = 0; ch < channels_; ++ch)
  {
    power[ch] = le_float(average_[ch].data()) * scales_[ch];
    valid &= std::isfinite(power[ch]) && std::abs(power[ch]) <= RBAMP_SANITY_LIMIT_P;
  }
  rbamp_energy_tick(&energy_, power.data(), channels_, (latchAt_ - previousLatch_) / 1000, valid);
  if(valid)
    energyValid_.fill(voltages_ != 0);
}

void Meter::decode_counter_energy()
{
  rbamp_std_energy_t current{};
  current.act_import_wh = rbampi_le_i64(&counters_[0]);
  current.act_export_wh = rbampi_le_i64(&counters_[8]);
  current.act_net_wh = rbampi_le_i64(&counters_[16]);
  current.act_import_mwh = rbampi_le_i64(&counters_[40]);
  current.act_export_mwh = rbampi_le_i64(&counters_[48]);
  current.act_net_mwh = rbampi_le_i64(&counters_[56]);
  current.generation = rbampi_le_u32(&counters_[64]);
  current.uptime_s = rbampi_le_u32(uptime_.data());
  current.channels = map_ >= 0x36 && groupStatus_ <= 3 && groupStatus_ != 2 ? channels_ : 0;
  for(std::uint8_t ch = 0; ch < current.channels; ++ch)
  {
    current.ch_import_mwh[ch] = rbampi_le_i64(channelEnergy_[ch].data());
    current.ch_export_mwh[ch] = rbampi_le_i64(&channelEnergy_[ch][8]);
  }
  rbampi_energy_fill_coarse(&current, false, false, map_ >= 0x36);
  if(caps_ & RBAMP_STD_CAP_ENERGY_UWH)
  {
    rbampi_energy_decode_fine(&current, fineCounters_.data(), firmware_ >= RBAMPI_FW_UVARH_MIN);
    for(std::uint8_t ch = 0; ch < current.channels; ++ch)
    {
      current.ch_import_uwh[ch] = rbampi_le_i64(channelEnergy_[ch].data());
      current.ch_export_uwh[ch] = rbampi_le_i64(&channelEnergy_[ch][8]);
    }
  }
  rbamp_std_energy_t delta{};
  const auto result = counterAnchored_ ? rbamp_std_energy_delta(&previousCounters_, &current, &delta, nullptr) : ESP_OK;
  if(result == ESP_OK)
  {
    for(std::uint8_t ch = 0; ch < channels_; ++ch)
    {
      if(current.channels != 0 && (!counterAnchored_ || delta.channels == current.channels))
      {
        rbamp_energy_add_wh(&energy_, ch, (delta.ch_import_uwh[ch] - delta.ch_export_uwh[ch]) / 1000000.0);
        energyValid_[ch] = true;
      }
      else if(channels_ == 1)
      {
        rbamp_energy_add_wh(&energy_, ch, delta.act_net_uwh / 1000000.0);
        energyValid_[ch] = true;
      }
    }
  }
  previousCounters_ = current;
  counterAnchored_ = true;
}

void Meter::finish_sample(std::int64_t now)
{
  const auto uptime = rbampi_le_u32(uptime_.data());
  const bool restarted = uptime < lastUptime_;
  lastUptime_ = uptime;
  if(restarted)
    return fail(ESP_ERR_INVALID_STATE, now); // Re-detect and restore volatile module settings.
  const bool empty = !std_ ? (valid_ & 1) == 0 : (caps_ & RBAMP_STD_CAP_MULTI_LATCH) && ((valid_ & 1) == 0 || rbampi_le_u32(&latch_[RBAMP_STD_LATCH_WIN_CNT]) == 0);
  emptyPeriods_ = empty ? emptyPeriods_ + 1 : 0;
  if(emptyPeriods_ >= 3)
  {
    emptyPeriods_ = 0;
    return fail(ESP_ERR_INVALID_RESPONSE, now); // A reset may have disarmed STD bank 0.
  }
  if(!std_)
    decode_basic_energy();
  else if(caps_ & RBAMP_STD_CAP_MULTI_LATCH)
  {
    rbamp_std_latch_t latch{};
    latch.channels = channels_;
    latch.nsamp = rbampi_le_u32(&latch_[RBAMP_STD_LATCH_NSAMP]);
    latch.valid = (valid_ & 1) != 0 && rbampi_le_u32(&latch_[RBAMP_STD_LATCH_WIN_CNT]) != 0;
    for(std::uint8_t ch = 0; ch < channels_; ++ch)
    {
      latch.e_import_ch_mws[ch] = rbampi_le_i64(channelEnergy_[ch].data());
      latch.e_export_ch_mws[ch] = rbampi_le_i64(&channelEnergy_[ch][8]);
      double import = 0, exportWh = 0;
      if(latch.valid && anchored_ && rbamp_std_latch_wh(&latch, (latchAt_ - previousLatch_) / 1000, ch, &import, &exportWh) == ESP_OK)
      {
        rbamp_energy_add_wh(&energy_, ch, import + exportWh);
        energyValid_[ch] = true;
      }
    }
  }
  else if(caps_ & RBAMP_STD_CAP_ONDEVICE_ENERGY)
    decode_counter_energy();

  // An empty/stale latch does not consume a period; retain the last consumed
  // anchor just like the upstream energy API.
  const bool periodConsumed = !std_ ? (valid_ & 1) != 0 : (caps_ & RBAMP_STD_CAP_MULTI_LATCH) == 0 || ((valid_ & 1) != 0 && rbampi_le_u32(&latch_[RBAMP_STD_LATCH_WIN_CNT]) != 0);
  if(periodConsumed)
    previousLatch_ = latchAt_;
  anchored_ = true;
  const float hz = std_ ? rbampi_le_u16(frequency_.data()) * 0.01f : frequency_[0];
  if(hz >= 40 && hz <= 70)
    sample_.frequency = hz;
  for(std::uint8_t ch = 0; ch < channels_; ++ch)
  {
    auto &out = sample_.channels[ch];
    const auto *p = current_[ch].data();
    quantity(std_ ? rbampi_le_i32(p) * 1e-6f : le_float(p) * scales_[ch], out.current, RBAMP_SANITY_LIMIT_I);
    if(std_ ? map_ > 0x34 : true)
      quantity(std_ ? rbampi_le_i32(p + RBAMP_STD_CURRENT_I_PEAK) * 1e-6f : le_float(p + 4) * scales_[ch], out.peakCurrent, RBAMP_SANITY_LIMIT_I);
    const auto voltage = std_ ? associations_[ch] : 0;
    if(voltage < voltages_)
    {
      const auto *v = voltage_[voltage].data();
      quantity(std_ ? rbampi_le_i32(v) * 0.001f : le_float(v), out.voltage, RBAMP_SANITY_LIMIT_V);
      if(!std_ || map_ > 0x34)
        quantity(std_ ? rbampi_le_i32(v + 4) * 0.001f : le_float(v + 4), out.peakVoltage, RBAMP_SANITY_LIMIT_V);
      quantity(std_ ? rbampi_le_i32(p + 4) * 0.001f : le_float(p + 8) * scales_[ch], out.power, RBAMP_SANITY_LIMIT_P);
      quantity(std_ ? rbampi_le_i16(p + RBAMP_STD_CURRENT_PF) / 1024.0f : le_float(p + 12), out.powerFactor, RBAMP_SANITY_LIMIT_PF);
      if(std_)
      {
        quantity(rbampi_le_i32(p + RBAMP_STD_CURRENT_S) * 0.001f, out.apparentPower, RBAMP_SANITY_LIMIT_P);
        if(caps_ & RBAMP_STD_CAP_REACTIVE)
          quantity(rbampi_le_i32(p + RBAMP_STD_CURRENT_Q) * 0.001f, out.reactivePower, RBAMP_SANITY_LIMIT_P);
        if(caps_ & RBAMP_STD_CAP_THD)
        {
          quantity(rbampi_le_u16(v + RBAMP_STD_VOLTAGE_THD_U) * 0.01f, out.voltageThd, 655.35f);
          if(configuration_[6] != 0)
            quantity(rbampi_le_u16(p + RBAMP_STD_CURRENT_THD_I) * 0.01f, out.currentThd, 655.35f);
        }
      }
      if(energyValid_[ch])
        out.energy = rbamp_energy_wh_get(&energy_, ch);
    }
  }
}

bool Meter::take(Reading &sample)
{
  if(!complete_)
    return false;
  sample = sample_;
  complete_ = false;
  return true;
}
} // namespace rbamp_async
