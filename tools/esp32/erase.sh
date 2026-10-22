
#!/usr/bin/env bash
set -euo pipefail

PORT="${ESPPORT:-/dev/ttyUSB0}"

echo "WARNING: This will erase the entire ESP32 flash memory ($PORT)."
read -r -p "Continue? [y/N] " answer

if [[ ! "$answer" =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
fi

esptool --chip esp32 --port "$PORT" erase-flash "$@"
