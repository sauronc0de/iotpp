#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

bash "$SCRIPT_DIR/build.sh"
exec "$ROOT_DIR/host/build/dex-cli/dex-client" "$@"
