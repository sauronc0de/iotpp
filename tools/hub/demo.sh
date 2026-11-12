#!/usr/bin/env bash
# Hub + simulated devices in one command, with a separate demo database.
#   run hub demo               -> http://localhost:8080
#   run hub demo --devices 5   (arguments go to the simulator)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/../.utilities/host-build.sh"

build_host all
mkdir -p "$ROOT_DIR/host/data"
"$ROOT_DIR/host/build/sim/iotpp-sim" "$@" &
SIM_PID=$!
trap 'kill $SIM_PID 2>/dev/null || true' EXIT
"$ROOT_DIR/host/build/hub/iotpp-hub" --db "$ROOT_DIR/host/data/demo.db" --no-discover
