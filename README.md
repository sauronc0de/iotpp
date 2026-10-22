# iotpp

`run` is the main project tool: `run <category> <command> [arguments...]` runs `tools/<category>/<command>.sh`, forwarding any arguments. Available in the devcontainer terminal.

```sh
run esp32 build   # Build firmware
run esp32 erase   # Erase all flash (asks for confirmation)
run esp32 flash   # Flash firmware
```

Default port: `/dev/ttyUSB0`. Override it with `ESPPORT=/dev/ttyUSB1 run esp32 flash`.
