#!/usr/bin/env bash
set -euo pipefail

# Ignore unmatched wildcards
shopt -s nullglob

# Find USB serial adapters
ports=(/dev/ttyUSB* /dev/ttyACM*)

if (( ${#ports[@]} == 0 )); then
    echo "ERROR: No USB serial device found."
    exit 1
fi

if (( ${#ports[@]} > 1 )); then
    printf 'Detected ports:\n%s\n' "${ports[@]}"
    echo "ERROR: Multiple serial devices found."
    exit 1
fi

port="${ports[0]}"

echo "ESP32 serial port: $port"

# Verify communication with the ESP32
esptool --port "$port" chip-id