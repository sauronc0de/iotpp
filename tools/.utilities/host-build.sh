#!/usr/bin/env bash

HOST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../host" && pwd)"
HOST_BUILD_DIR="$HOST_DIR/build"

build_host() {
    local target="$1"
    shift

    local build_cli=OFF build_hub=OFF
    local targets=()
    case "$target" in
        dex-client)
            build_cli=ON
            targets=(--target dex-client)
            ;;
        iotpp-hub)
            build_hub=ON
            targets=(--target iotpp-hub)
            ;;
        all)
            build_cli=ON
            build_hub=ON
            ;;
        *)
            echo "Unknown host target: $target" >&2
            return 1
            ;;
    esac

    cmake -S "$HOST_DIR" -B "$HOST_BUILD_DIR" \
        -D "IOTPP_BUILD_DEX_CLI=$build_cli" \
        -D "IOTPP_BUILD_HUB=$build_hub"
    cmake --build "$HOST_BUILD_DIR" "${targets[@]}" --parallel "$@"
}
