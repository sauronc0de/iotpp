#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/src/build/dex-client"

cmake -S "$ROOT_DIR/src/app/dex-client" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --parallel "$@"
