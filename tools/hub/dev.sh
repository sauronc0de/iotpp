#!/usr/bin/env bash
# Run the hub serving the web UI straight from host/hub/web: edit and reload
# the browser, no rebuild needed for frontend changes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
exec bash "$SCRIPT_DIR/start.sh" --web-root "$ROOT_DIR/host/hub/web" "$@"
