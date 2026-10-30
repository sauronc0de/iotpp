#pragma once

#include "config_store.hpp"
#include "dex_server.hpp"

namespace pikachu
{
class Control
{
public:
  Control(std::uint8_t peripheralId, std::uint32_t sampleMs, std::uint32_t publishMs) : peripheralId_(peripheralId)
  {
    config_.monitoring.samplePeriodMs = sampleMs;
    config_.monitoring.publishPeriodMs = publishMs;
  }

  const dex::Pikachu::ConfigData &config() const { return config_; }
  std::uint32_t revision() const { return revision_; }

  static bool valid(const dex::Pikachu::ConfigData &config)
  {
    const auto &m = config.monitoring;
    return m.enabled <= 1 && m.samplePeriodMs >= 1000 && m.samplePeriodMs <= 60000 &&
           m.publishPeriodMs >= m.samplePeriodMs && m.publishPeriodMs <= 3600000;
  }

  config_store::Result restore()
  {
    auto candidate = config_;
    const auto result = config_store::load_config({25, peripheralId_}, candidate);
    if(result != config_store::Result::Ok)
      return result;
    if(!valid(candidate))
      return config_store::Result::StorageFailed;
    config_ = candidate;
    ++revision_;
    return config_store::Result::Ok;
  }

  dex::ProcessResult process(const dex::Request &request, std::uint8_t *response, std::size_t capacity)
  {
    const auto reply = [&](dex::Error error, dex::Bytes data = {}) {
      const auto encoded = dex::encode_response({request.command, error, data}, response, capacity);
      return dex::ProcessResult{encoded.error, encoded.size, dex::Action::None};
    };
    if(request.command != dex::CommandId::PeripheralConfig)
      return reply(dex::Error::UnsupportedOperation);
    if(request.data.data == nullptr || request.data.size == 0 || request.data.data[0] != peripheralId_)
      return reply(dex::Error::InvalidParameter);
    dex::peripheral::AddressedData<dex::Pikachu::ConfigData> candidate{peripheralId_, config_};
    if(request.data.size == 1)
      return reply(dex::Error::None, dex::as_bytes(candidate));
    if(dex::decode_data(request.data, candidate) != dex::Error::None || !valid(candidate.data))
      return reply(dex::Error::InvalidParameter);
    const auto encoded = reply(dex::Error::None, dex::as_bytes(candidate));
    if(encoded.error != dex::Error::None)
      return encoded;
    const auto saved = config_store::save_config({25, peripheralId_}, candidate.data);
    if(saved != config_store::Result::Ok)
      return reply(saved == config_store::Result::NotReady ? dex::Error::NotReady : dex::Error::StorageFailed);
    if(std::memcmp(&config_, &candidate.data, sizeof(config_)) != 0)
    {
      config_ = candidate.data;
      ++revision_;
    }
    return encoded;
  }

private:
  std::uint8_t peripheralId_;
  dex::Pikachu::ConfigData config_{};
  std::uint32_t revision_ = 0;
};
} // namespace pikachu
