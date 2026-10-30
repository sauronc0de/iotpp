#include "ota_peripheral.hpp"

#include <cstring>
#include "ota.hpp"

dex::ProcessResult OtaPeripheral::process(const dex::Request &request, std::uint8_t *response, std::size_t capacity) const
{
  const auto reply = [&](dex::Error error, dex::Bytes data = {}) {
    const auto encoded = dex::encode_response({request.command, error, data}, response, capacity);
    return dex::ProcessResult{encoded.error, encoded.size, dex::Action::None};
  };
  if(!request.data.data || !request.data.size || request.data.data[0] != instance_)
    return reply(dex::Error::InvalidParameter);
  const dex::Bytes data{request.data.data + 1, request.data.size - 1};
  switch(request.command)
  {
  case dex::CommandId::PeripheralStatus:
  {
    if(data.size != 0)
      return reply(dex::Error::InvalidParameter);
    dex::peripheral::AddressedData<dex::Mew::StatusData> packet{};
    packet.peripheralId = instance_;
    auto &wire = packet.data;
    const auto status = ota::status();
    wire.state = status.phase == ota::Phase::Error ? dex::State::Warning :
        status.phase == ota::Phase::Idle ? dex::State::Ready : dex::State::Busy;
    wire.errors = status.lastError == ESP_OK ? dex::Error::None : dex::Error::FirmwareFailed;
    wire.notifications = dex::Notification::None;
    wire.phase = static_cast<dex::Mew::Phase>(status.phase);
    wire.lastError = status.lastError;
    wire.downloaded = status.downloaded;
    wire.total = status.total;
    wire.lastCheckUtc = status.lastCheckUtc;
    std::memcpy(wire.candidate, status.candidate, sizeof(wire.candidate));
    for(unsigned i = 0; i < 2; ++i)
    {
      const auto &slot = status.slots[i];
      auto &target = wire.slots[i];
      std::memcpy(target.label, slot.label, sizeof(target.label));
      std::memcpy(target.version, slot.version, sizeof(target.version));
      target.state = static_cast<dex::Mew::SlotState>(slot.state);
      target.running = slot.running;
      target.nextBoot = slot.nextBoot;
    }
    return reply(dex::Error::None, dex::as_bytes(packet));
  }
  case dex::CommandId::PeripheralConfig:
  {
    if(data.size)
    {
      dex::Mew::ConfigData candidate;
      if(dex::decode_data(data, candidate) != dex::Error::None || candidate.intervalSeconds > 2592000)
        return reply(dex::Error::InvalidParameter);
      if(ota::configure({candidate.intervalSeconds}) != ESP_OK)
        return reply(dex::Error::StorageFailed);
    }
    const dex::peripheral::AddressedData<dex::Mew::ConfigData> packet{instance_, {ota::configuration().intervalSeconds}};
    return reply(dex::Error::None, dex::as_bytes(packet));
  }
  case dex::CommandId::PeripheralAction:
  {
    dex::Mew::ActionData action;
    if(dex::decode_data(data, action) != dex::Error::None || action.action != dex::Mew::Action::FirmwareCheck)
      return reply(dex::Error::InvalidParameter);
    if(ota::firmware_check() != ESP_OK)
      return reply(dex::Error::NotReady);
    return reply(dex::Error::None, {&instance_, 1});
  }
  default:
    return reply(dex::Error::UnsupportedOperation);
  }
}
