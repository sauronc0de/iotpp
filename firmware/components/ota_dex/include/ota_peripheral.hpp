#pragma once

#include "dex_server.hpp"

// Mew is a protocol adapter. All OTA work runs in the independent ota service.
class OtaPeripheral
{
public:
  explicit OtaPeripheral(std::uint8_t instance) : instance_(instance) {}
  dex::ProcessResult process(const dex::Request &, std::uint8_t *, std::size_t) const;

private:
  std::uint8_t instance_;
};
