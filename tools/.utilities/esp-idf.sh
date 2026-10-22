#!/usr/bin/env bash

# Shared project settings for build, flash and monitor, independent of cwd.
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../src" && pwd)"
BUILD_DIR="$PROJECT_DIR/build/target-analyzer"

if ! command -v idf.py >/dev/null 2>&1; then
    if [[ ! -f "${IDF_PATH:-/opt/esp/idf}/export.sh" ]]; then
        echo "ERROR: ESP-IDF not found. Activate its export.sh first." >&2
        exit 1
    fi
    source "${IDF_PATH:-/opt/esp/idf}/export.sh" >/dev/null
fi

run_idf() {
    idf.py -C "$PROJECT_DIR" -B "$BUILD_DIR" --preset target-analyzer \
        -D APP_NAME=analyzer -D IDF_TARGET=esp32 \
        -D "SDKCONFIG=$PROJECT_DIR/config/build/sdkconfig" "$@"
}
