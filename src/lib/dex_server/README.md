# DEX server

ESP-IDF component providing `dex::Server`, a blocking UDP server using the
packet definitions from `../dex`.

Initialize Wi-Fi before configuring and running the server:

```cpp
#include "dex_server.hpp"
#include "wifi.h"

ESP_ERROR_CHECK(wifi::initialize(ssid, password));
dex::Server server;
ESP_ERROR_CHECK(server.initialize(4444, dex::Version{0, 0, 0}));
ESP_ERROR_CHECK(server.run());
```

Add `dex_server` to the application's `PRIV_REQUIRES`. The component exports
the DEX headers through its public dependency.

`initialize()` stores the port and firmware version and rejects port zero or
repeated initialization. `run()` waits for a Wi-Fi connection, opens the UDP
socket, and serves requests indefinitely, retrying socket failures. Call it
from a single task; it does not create a background task.

Supported commands:

- `Info`: firmware version, no peripherals (`device::InfoData<0>`).
- `Status`: ready, no errors or notifications.
- `Reset`: acknowledge before restarting the device.

Unexpected payloads, unsupported commands and oversized datagrams return DEX
errors. Empty datagrams are ignored. Replies use the existing packed DEX
structures, including the storage byte of the empty peripheral array.
