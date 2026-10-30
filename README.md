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

## Select device tasks

Run `run esp32 menuconfig` and open **Device tasks** to enable or disable tasks.
Save, then run `run esp32 build` and flash the resulting firmware. The selection
is stored in the local `firmware/config/build/sdkconfig`. Fresh configurations enable
`server_dex`, `monitor_rbamp` and `ota`; other tasks start disabled.

The menu automatically discovers every `firmware/tasks/<name>/CMakeLists.txt`.
Adding a new task module makes its checkbox appear on the next configuration or
build, without editing a task registry or the menu. A task may also provide
`firmware/tasks/<name>/Kconfig`; its settings are automatically included when enabled.
Only selected tasks contribute sources and startup calls to the firmware.

## OTA updates — Mew (type 151)

Sentinel includes an independent OTA task: HTTPS checks every six hours,
automatic installation of compatible newer firmware, two OTA slots and boot
rollback. Mew exposes both slots, versions, running/next-boot flags, progress,
persistent polling configuration and a manual `FirmwareCheck` action over DEX.

```sh
run esp32 menuconfig                            # Set OTA manifest URL
run esp32 build -D FIRMWARE_RELEASE=1.0.0 -D OTA_CA_CERT=/path/to/root-ca.crt
run dex client mew-status --id 2
run dex client mew-check --id 2
run dex client mew-config --id 2 --interval 0     # Manual-only checks
```

## Sentinel DEX listener

The native Linux C++ client links `libs/dex` and prints decoded enums using
`magic_enum`.

```sh
run dex build
run dex client                                 # Same as all; builds first
run dex client info
run dex client status
run dex client config
run dex client all 192.168.41.252 --port 4444
run dex client monitor                          # Listen on 0.0.0.0:4444 until Ctrl+C
run dex client monitor 192.168.41.252 --port 4444 # Only accept publications from this device
run dex client monitor --port 5555               # Override the local listening port
```

`monitor` listens locally on all IPv4 interfaces. The optional host is the
device's address and filters incoming publications by sender IP; omitting it
accepts publications from any device. It continuously receives UDP publications and prints the sender,
peripheral instance, acquisition timestamp (Unix UTC seconds), and decoded
Snorlax door state or Pikachu electrical measurements. Invalid publications are
reported and listening continues. Configure **Door GPIO monitoring → Door event
receiver hostname or IPv4** (`CONFIG_DOOR_MONITOR_HOST`) or the RBAMP monitoring
receiver to the IP of the machine running the client, with the same UDP port.
The listener must be reachable over UDP from the device.

The sentinel receives binary DEX requests on UDP port `4444` (configurable) and
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
| `Info` | None | Firmware version + typed peripheral entries when OTA is enabled; otherwise `dex::device::InfoData<0>` |
| `Status` | None | `dex::StatusData` (ready, no errors or notifications) |
| `Reset` | None | None; replies OK before restarting |

Unexpected DATA returns `InvalidParameter`; unsupported commands return
`UnsupportedOperation`. Datagrams over 1200 bytes return `OutOfRange`; empty
datagrams are ignored. Every reply echoes CMD and includes RESULT.
