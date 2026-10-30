# DEX Protocol

**Device Exchange Protocol** — A header-only C++17 library for binary communication over UDP. No external dependencies.

```text
Controller  ── CMD + DATA ───────────► Device
Controller  ◄── CMD + RESULT + DATA ── Device
```

- **CMD**: one-byte operation, echoed in the response.
- **RESULT**: four-byte `dex::Error` bitmask (`None` = OK).
- **DATA**: optional packed struct, sent directly in native little-endian order.
- One message per datagram, up to 1200 bytes. Publications use the response format.

Include `dex.hpp`. Send `RequestHeader` / `ResponseHeader` for empty messages or
`RequestPacket<T>` / `ResponsePacket<T>` with payloads. No field serialization.
DEX data structs are packed; custom payloads must also be packed and contain no pointers.
`as_bytes` borrows an object's bytes; `decode_data` checks the size and copies them
into an object. It does not validate field values. Empty structs occupy one byte;
use a header-only message when there is no DATA.

`decode_request` / `decode_response` return `dex::Error` and borrow the receive buffer.
`encode_request` / `encode_response` remain available to assemble a buffer from raw
payload bytes. The application handles UDP transport and validates payload values.

## Example

Send a status directly and process it on the other side. `udp_send` and
`handle_status` are application-provided; pass the actual received datagram length.

```cpp
#include "dex.hpp"

void send_status()
{
  dex::ResponsePacket<dex::StatusData> packet{
      dex::CommandId::Status, dex::Error::None,
      {dex::State::Ready, dex::Error::None, dex::Notification::None}};
  udp_send(&packet, sizeof(packet)); // Send/copy before this object expires.
}

void on_receive(const std::uint8_t* packet, std::size_t size)
{
  dex::Response response;
  if(dex::decode_response({packet, size}, response) != dex::Error::None)
    return;
  if(response.command != dex::CommandId::Status || response.result != dex::Error::None)
    return;

  dex::StatusData status{};
  if(dex::decode_data(response.data, status) == dex::Error::None)
    handle_status(status);
}
```

Both peers use the same struct definitions. This little-endian format replaces
the previous big-endian codec; update both peers together.
