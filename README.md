# iotpp

`run` is the main project tool: `run <category> <command> [arguments...]` runs `tools/<category>/<command>.sh`, forwarding any arguments. Available in the devcontainer terminal.

```sh
run esp32 build   # Build firmware
run esp32 erase   # Erase all flash (asks for confirmation)
run esp32 flash   # Flash firmware
run dex build     # Build the native Linux DEX client
run dex client    # Build incrementally and query Info, Status and Config
```

Default port: `/dev/ttyUSB0`. Override it with `ESPPORT=/dev/ttyUSB1 run esp32 flash`.

## Analyzer DEX listener

The native Linux C++ client links `lib/dex` and prints decoded enums using
`magic_enum`. See [`src/app/dex-client`](src/app/dex-client/README.md).

```sh
run dex build
run dex client                                 # Same as all; builds first
run dex client info
run dex client status
run dex client config
run dex client all 192.168.41.252 --port 4444
```

The analyzer receives binary DEX requests on UDP port `4444` (configurable) and
replies to the sender's IP and port using the raw little-endian structs in `dex.hpp`.

Query the device from the devcontainer using Bash, `timeout`, `dd` and `od`:

```sh
run dex request                                 # Same as all
run dex request all                             # Info, Status, Config in sequence
run dex request info                            # Firmware version
run dex request status                          # State, errors and notifications
run dex request config                          # Read configuration
run dex request all 192.168.41.252 --port 4444    # Override the target
```

The default target is `192.168.41.252:4444`. Each query waits for a reply before
the next is sent, with a timeout of 5 seconds. `all` continues after a failed
query and exits with a nonzero status if any query fails. The current listener
returns `UnsupportedOperation` for Config. The device must be reachable over
UDP from the container.

| CMD | Request DATA | Successful response DATA |
|---|---|---|
| `Info` | None | `dex::device::InfoData<0>` (firmware version, no peripherals) |
| `Status` | None | `dex::StatusData` (ready, no errors or notifications) |
| `Reset` | None | None; replies OK before restarting |

Unexpected DATA returns `InvalidParameter`; unsupported commands return
`UnsupportedOperation`. Datagrams over 1200 bytes return `OutOfRange`; empty
datagrams are ignored. Every reply echoes CMD and includes RESULT.
