#!/usr/bin/env bash
# Build and run the hub. Extra arguments go to iotpp-hub (see --help).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
DATA_DIR="${IOTPP_HUB_DATA:-$ROOT_DIR/host/data}"

bash "$SCRIPT_DIR/build.sh"
mkdir -p "$DATA_DIR"
exec "$ROOT_DIR/host/build/hub/iotpp-hub" --db "$DATA_DIR/iotpp-hub.db" "$@"
