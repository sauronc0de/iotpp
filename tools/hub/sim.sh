#!/usr/bin/env bash
# Build and run the ESP32 simulator (see --help): fake sentinels on
# 127.0.0.10, .11, ... publishing to the hub on 127.0.0.1:4444.
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/../.utilities/host-build.sh"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

build_host iotpp-sim
exec "$ROOT_DIR/host/build/sim/iotpp-sim" "$@"
