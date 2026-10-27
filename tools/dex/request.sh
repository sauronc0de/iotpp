#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: run dex request [info|status|config|all] [host] [--port PORT]"
    echo "Defaults: all, 192.168.41.252, port 4444; timeout: 5 seconds per request."
}

command=all
host=192.168.41.252
port=4444
positional=0
while (( $# )); do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --port)
            if (( $# < 2 )); then
                usage >&2
                exit 1
            fi
            port=$2
            shift 2
            ;;
        -*) usage >&2; exit 1 ;;
        *)
            case "$positional" in
                0) command=$1 ;;
                1) host=$1 ;;
                *) usage >&2; exit 1 ;;
            esac
            positional=$((positional + 1))
            shift
            ;;
    esac
done

case "$command" in
    info|status|config) commands=("$command") ;;
    all) commands=(info status config) ;;
    *) usage >&2; exit 1 ;;
esac
if [[ ! "$port" =~ ^[0-9]{1,5}$ ]] || (( 10#$port < 1 || 10#$port > 65535 )); then
    echo "Invalid UDP port: $port" >&2
    exit 1
fi
port=$((10#$port))

request() {
    local name=$1 id=$2 raw result state errors notifications
    local -a bytes
    printf '\n=== %s ===\n' "$name"
    # A new socket for each query prevents late replies from mixing commands.
    if ! exec 3<>"/dev/udp/$host/$port"; then
        echo "Cannot open UDP socket to $host:$port" >&2
        return 1
    fi
    # Each request contains only its command byte (Config is a read request).
    if ! printf '%b' "\\x$id" >&3; then
        exec 3>&-
        return 1
    fi
    # Keep binary data out of Bash variables: convert it to decimal bytes first.
    if ! raw=$(timeout 5 dd bs=65535 count=1 <&3 2>/dev/null | od -An -v -tu1); then
        exec 3>&-
        echo "DEX $name failed: timeout or UDP receive error" >&2
        return 1
    fi
    exec 3>&-
    read -r -a bytes <<< "${raw//$'\n'/ }"
    printf 'Response from %s:%s:' "$host" "$port"
    printf ' %02X' "${bytes[@]}"
    printf '\n'
    if (( ${#bytes[@]} < 5 )); then
        echo "Invalid DEX response: header is shorter than 5 bytes" >&2
        return 1
    fi
    result=$((bytes[1] | bytes[2] << 8 | bytes[3] << 16 | bytes[4] << 24))
    printf 'Command: 0x%02X; Result: 0x%08X\n' "${bytes[0]}" "$result"
    if (( bytes[0] != 16#$id )); then
        echo "Invalid DEX response: unexpected command" >&2
        return 1
    fi
    if (( result != 0 )); then
        if (( result & 512 )); then
            echo "UnsupportedOperation: the device does not support this query" >&2
        fi
        return 1
    fi
    case "$name" in
        info)
            if (( ${#bytes[@]} < 8 )); then
                echo "Invalid Info response: missing firmware version" >&2
                return 1
            fi
            printf 'Firmware version: %d.%d.%d\n' "${bytes[5]}" "${bytes[6]}" "${bytes[7]}"
            ;;
        status)
            if (( ${#bytes[@]} < 15 )); then
                echo "Invalid Status response: missing status data" >&2
                return 1
            fi
            state=$((bytes[5] | bytes[6] << 8))
            errors=$((bytes[7] | bytes[8] << 8 | bytes[9] << 16 | bytes[10] << 24))
            notifications=$((bytes[11] | bytes[12] << 8 | bytes[13] << 16 | bytes[14] << 24))
            printf 'State: 0x%04X; Errors: 0x%08X; Notifications: 0x%08X\n' "$state" "$errors" "$notifications"
            ;;
        config)
            if (( ${#bytes[@]} == 5 )); then
                echo "Configuration: no data"
            else
                printf 'Configuration data:'
                printf ' %02X' "${bytes[@]:5}"
                printf '\n'
            fi
            ;;
    esac
}

exit_code=0
for command in "${commands[@]}"; do
    case "$command" in
        info) id=00 ;;
        status) id=01 ;;
        config) id=02 ;;
    esac
    if ! request "$command" "$id"; then
        exit_code=1
    fi
done
exit "$exit_code"
