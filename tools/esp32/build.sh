#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/../.utilities/esp-idf.sh"

run_idf build "$@"
