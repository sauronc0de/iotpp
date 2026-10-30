# DEX server

Portable C++ library providing `dex::Server`, a request processor using the
packet definitions from `../dex`. Networking and device actions belong to the caller.

Process a received request into a caller-owned response buffer:

```cpp
#include "dex_server.hpp"

dex::Server server{dex::Version{0, 0, 0}};
std::uint8_t response[1200];
const auto result = server.process(request, response, sizeof(response));
// Send result.responseSize bytes, then handle result.action.
```

Link the `dex_server` CMake target. It exports the DEX headers through its
public dependency and has no ESP-IDF or Linux dependencies. The ESP32 transport
and restart handling live in `firmware/tasks/server_dex` at the repository root.

Supported commands:

- `Info`: firmware version, no peripherals (`device::InfoData<0>`).
- `Status`: ready, no errors or notifications.
- `Reset`: acknowledge before restarting the device.

Unexpected payloads, unsupported commands and oversized datagrams return DEX
errors. Empty datagrams are ignored. Replies use the existing packed DEX
structures, including the storage byte of the empty peripheral array.
