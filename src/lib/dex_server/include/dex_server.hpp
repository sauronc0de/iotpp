#pragma once

#include "dex.hpp"

namespace dex
{
enum class Action
{
  None,
  Restart
};

struct ProcessResult
{
  Error error = Error::None; // Local processing/encoding error, not the wire RESULT.
  std::size_t responseSize = 0;
  Action action = Action::None;
};

using PeripheralHandler = ProcessResult (*)(const Request &, std::uint8_t *, std::size_t);

class Server
{
public:
  explicit Server(Version firmwareVersion, PeripheralHandler peripheralHandler = nullptr,
                  PeripheralHandler infoHandler = nullptr);

  // Write a reply into the caller's buffer. Empty datagrams produce no reply.
  // Execute the returned action only after successfully sending the reply.
  ProcessResult process(Bytes request, std::uint8_t *response, std::size_t capacity) const;

private:
  device::InfoData<0> info_{};
  PeripheralHandler peripheralHandler_ = nullptr;
  PeripheralHandler infoHandler_ = nullptr;
};
} // namespace dex
