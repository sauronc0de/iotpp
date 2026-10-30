#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/../.utilities/host-build.sh"

build_host dex-client "$@"
