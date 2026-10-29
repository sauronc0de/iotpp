#pragma once

#include "dex_server.hpp"
#include "config_store.hpp"

namespace door
{
// The firmware serializes access from the monitor and DEX server tasks.
class Control
{
public:
  Control(std::uint8_t peripheralId, std::uint32_t samplePeriodMs) : peripheralId_(peripheralId)
  {
    config_.monitoring.samplePeriodMs = samplePeriodMs;
    config_.monitoring.publishPeriodMs = 0; // Event-driven, not periodic.
  }

  bool enabled() const { return config_.enabled != 0; }
  std::uint32_t revision() const { return revision_; }
  void observe(dex::Snorlax::State state) { doorState_ = state; }

  config_store::Result restore()
  {
    auto candidate = config_;
    const auto result = config_store::load_config({143, peripheralId_}, candidate);
    if(result != config_store::Result::Ok)
      return result;
    if(!valid(candidate))
      return config_store::Result::StorageFailed;
    config_ = candidate;
    ++revision_;
    return config_store::Result::Ok;
  }

  bool can_sleep() const
  {
    return enabled() && doorState_ == dex::Snorlax::State::Closed && !sleeping_;
  }

  // Commit under the same lock as Config SET. A successful disable therefore
  // always prevents sleep; a request after commitment is rejected as NotReady.
  bool begin_sleep()
  {
    if(!can_sleep())
      return false;
    sleeping_ = true;
    return true;
  }

  dex::ProcessResult process(const dex::Request &request, std::uint8_t *response, std::size_t capacity)
  {
    const auto reply = [&](dex::Error error, dex::Bytes data = {}) {
      const auto encoded = dex::encode_response({request.command, error, data}, response, capacity);
      return dex::ProcessResult{encoded.error, encoded.size, dex::Action::None};
    };
    const auto command = static_cast<dex::peripheral::PeripheralCommandId>(request.command);
    if(command != dex::peripheral::PeripheralCommandId::Status && command != dex::peripheral::PeripheralCommandId::Config)
      return reply(dex::Error::UnsupportedOperation);
    if(request.data.data == nullptr || request.data.size == 0 || request.data.data[0] != peripheralId_)
      return reply(dex::Error::InvalidParameter);

    if(command == dex::peripheral::PeripheralCommandId::Status)
    {
      if(request.data.size != 1)
        return reply(dex::Error::InvalidParameter);
      dex::peripheral::AddressedData<dex::Snorlax::StatusData> status{};
      status.peripheralId = peripheralId_;
      status.data.state = sleeping_ ? dex::State::Sleeping : !enabled() ? dex::State::Disabled :
                          doorState_ == dex::Snorlax::State::Unknown ? dex::State::Initializing : dex::State::Running;
      status.data.errors = dex::Error::None;
      status.data.notifications = dex::Notification::None;
      status.data.doorState = doorState_;
      return reply(dex::Error::None, dex::as_bytes(status));
    }

    dex::peripheral::AddressedData<dex::Snorlax::ConfigData> config{peripheralId_, config_};
    if(request.data.size != 1)
    {
      if(dex::decode_data(request.data, config) != dex::Error::None || config.data.enabled > 1)
        return reply(dex::Error::InvalidParameter);
      // Snorlax currently exposes only its top-level enable switch for writes.
      // The inherited monitoring fields describe the fixed acquisition profile.
      if(!valid(config.data))
        return reply(dex::Error::UnsupportedOperation);
      if(sleeping_)
        return reply(dex::Error::NotReady);
    }
    const auto result = reply(dex::Error::None, dex::as_bytes(config));
    if(result.error == dex::Error::None && request.data.size != 1)
    {
      // The whole ConfigData is persisted. No field list or byte size here.
      const auto saved = config_store::save_config({143, peripheralId_}, config.data);
      if(saved != config_store::Result::Ok)
        return reply(saved == config_store::Result::NotReady ? dex::Error::NotReady : dex::Error::StorageFailed);
      if(config_.enabled != config.data.enabled)
      {
        ++revision_;
        if(config.data.enabled)
          doorState_ = dex::Snorlax::State::Unknown;
      }
      config_ = config.data;
    }
    return result;
  }

private:
  bool valid(const dex::Snorlax::ConfigData &candidate) const
  {
    return candidate.enabled <= 1 &&
           candidate.monitoring.samplePeriodMs == config_.monitoring.samplePeriodMs &&
           candidate.monitoring.publishPeriodMs == config_.monitoring.publishPeriodMs &&
           candidate.monitoring.enabled == config_.monitoring.enabled;
  }

  std::uint8_t peripheralId_;
  dex::Snorlax::ConfigData config_{};
  dex::Snorlax::State doorState_ = dex::Snorlax::State::Unknown;
  std::uint32_t revision_ = 0;
  bool sleeping_ = false;
};
} // namespace door
