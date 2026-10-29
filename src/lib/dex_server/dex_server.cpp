#include "dex_server.hpp"

namespace dex
{
Server::Server(Version firmwareVersion, PeripheralHandler peripheralHandler)
    : info_{firmwareVersion, {}}, peripheralHandler_(peripheralHandler)
{
}

ProcessResult Server::process(Bytes bytes, std::uint8_t *response, std::size_t capacity) const
{
  if(bytes.size == 0)
    return {}; // No CMD to echo.
  if(bytes.data == nullptr)
    return {Error::InvalidParameter, 0, Action::None};

  const auto reply = [&](CommandId command, Error result, Bytes data = {}, Action action = Action::None) {
    const auto encoded = encode_response({command, result, data}, response, capacity);
    return ProcessResult{encoded.error, encoded.size, encoded ? action : Action::None};
  };

  Request request;
  const auto error = decode_request(bytes, request);
  if(error != Error::None)
    return reply(static_cast<CommandId>(bytes.data[0]), error);

  if(request.command == static_cast<CommandId>(peripheral::PeripheralCommandId::Status) ||
     request.command == static_cast<CommandId>(peripheral::PeripheralCommandId::Config))
  {
    if(peripheralHandler_ != nullptr)
      return peripheralHandler_(request, response, capacity);
    return reply(request.command, Error::UnsupportedOperation);
  }

  switch(request.command)
  {
  case CommandId::Info:
  case CommandId::Status:
  case CommandId::Reset:
    if(request.data.size != 0)
      return reply(request.command, Error::InvalidParameter);
    break;
  default:
    return reply(request.command, Error::UnsupportedOperation);
  }

  switch(request.command)
  {
  case CommandId::Info:
    return reply(request.command, Error::None, as_bytes(info_));
  case CommandId::Status:
  {
    const StatusData status{State::Ready, Error::None, Notification::None};
    return reply(request.command, Error::None, as_bytes(status));
  }
  case CommandId::Reset:
    return reply(request.command, Error::None, {}, Action::Restart);
  default:
    return reply(request.command, Error::UnsupportedOperation);
  }
}
} // namespace dex
