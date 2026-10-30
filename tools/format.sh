
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="$(basename "$0")"
VERSION="0.1.0"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel 2>/dev/null || printf '%s' "${SCRIPT_DIR}")"
SOURCE_DIRS=("${ROOT_DIR}/firmware" "${ROOT_DIR}/host" "${ROOT_DIR}/libs")
FORMAT_FILE="${ROOT_DIR}/.clang-format"

supports_color() {
  [ "${CLICOLOR_FORCE:-0}" != "0" ] && return 0
  [ -t 1 ] || return 1
  [ -z "${NO_COLOR:-}" ] || return 1
  case "${TERM:-}" in
    ''|dumb) return 1 ;;
  esac
  command -v tput >/dev/null 2>&1 || return 1
  [ "$(tput colors 2>/dev/null || printf '0')" -ge 8 ]
}

RESET=""
ACCENT=""
BAR=""
DIM=""
TEXT=""
RED=""
GREEN=""

if supports_color; then
  RESET=$'\033[0m'
  ACCENT=$'\033[38;5;45m'
  BAR=$'\033[38;5;111m'
  DIM=$'\033[2m'
  TEXT=$'\033[97m'
  RED=$'\033[38;5;203m'
  GREEN=$'\033[38;5;114m'
fi

usage() {
  cat <<EOF
SYNOPSIS
    ${SCRIPT_NAME} [--check] [--help|-h] [--version|-v]

DESCRIPTION
    Recursively format C/C++ files under firmware/, host/ and libs/ using the project's
    .clang-format configuration.

OPTIONS
    --check       Verify formatting without modifying files
    -h, --help    Show this help
    -v, --version Print the tool version

EXAMPLES
    ${SCRIPT_NAME}
    ${SCRIPT_NAME} --check
    NO_COLOR=1 ${SCRIPT_NAME}

IMPLEMENTATION
    version       ${VERSION}
EOF
}

err() {
  printf '%s[ERROR]%s %s\n' "${RED}" "${RESET}" "$*" >&2
}

check=false

while [ "$#" -gt 0 ]; do
  case "$1" in
    --check)
      check=true
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --short-help)
      printf '%s\n' "Format C/C++ files in firmware/, host/ and libs/ using .clang-format."
      exit 0
      ;;
    --version|-v)
      printf '%s\n' "${VERSION}"
      exit 0
      ;;
    *)
      err "Unknown option: $1"
      exit 2
      ;;
  esac
  shift
done

command -v clang-format >/dev/null 2>&1 || {
  err "clang-format is not installed"
  exit 1
}

for source_dir in "${SOURCE_DIRS[@]}"; do
  [ -d "${source_dir}" ] || {
    err "Source directory not found: ${source_dir}"
    exit 1
  }
done

[ -f "${FORMAT_FILE}" ] || {
  err "Configuration not found: ${FORMAT_FILE}"
  exit 1
}

mapfile -d '' files < <(
  find "${SOURCE_DIRS[@]}" \
    -type d \( -name build -o -name managed_components \) -prune -o \
    -type f \
    \( -name '*.c' -o -name '*.h' \
    -o -name '*.cpp' -o -name '*.hpp' \
    -o -name '*.cc' -o -name '*.hh' \
    -o -name '*.cxx' -o -name '*.hxx' \) \
    -print0 | sort -z
)

total="${#files[@]}"

if [ "${total}" -eq 0 ]; then
  printf '%sNo source files found.%s\n' "${DIM}" "${RESET}"
  exit 0
fi

frames=("⠁" "⠃" "⠇" "⠧" "⠷" "⠿" "⠷" "⠯" "⠮" "⠟")
width=24
failed=0
processed=0
start="${SECONDS}"

restore_terminal() {
  if [ -t 1 ]; then
    printf '\033[?25h'
  fi
}

trap restore_terminal EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ -t 1 ]; then
  printf '\033[?25l'
fi

render_progress() {
  local current="$1"
  local filename="$2"
  local percent=$((current * 100 / total))
  local filled=$((current * width / total))
  local empty=$((width - filled))
  local bar_filled=""
  local bar_empty=""
  local frame="${frames[$((current % ${#frames[@]}))]}"
  local label="Formatting"

  if "${check}"; then
    label="Checking"
  fi

  printf -v bar_filled '%*s' "${filled}" ''
  printf -v bar_empty '%*s' "${empty}" ''
  bar_filled="${bar_filled// /█}"
  bar_empty="${bar_empty// /░}"

  if [ -t 1 ]; then
    printf '\r\033[2K%s%s%s %s%-12s%s [%s%s%s%s%s] %s%3d%%%s %s(%d/%d)%s' \
      "${ACCENT}" "${frame}" "${RESET}" \
      "${TEXT}" "${label}" "${RESET}" \
      "${BAR}" "${bar_filled}" "${DIM}" "${bar_empty}" "${RESET}" \
      "${TEXT}" "${percent}" "${RESET}" \
      "${DIM}" "${current}" "${total}" "${RESET}"
  else
    printf '%3d%% (%d/%d) %s\n' \
      "${percent}" "${current}" "${total}" "${filename}"
  fi
}

printf '%sclang-format%s %s%s%s\n' \
  "${ACCENT}" "${RESET}" "${DIM}" "${SOURCE_DIRS[*]}" "${RESET}"

for file in "${files[@]}"; do
  relative="${file#"${ROOT_DIR}/"}"

  if "${check}"; then
    if ! clang-format --style="file:${FORMAT_FILE}" \
      --dry-run --Werror "${file}" >/dev/null 2>&1; then
      ((failed += 1))
      if [ -t 1 ]; then
        printf '\r\033[2K'
      fi
      printf '%s✗%s %s\n' "${RED}" "${RESET}" "${relative}"
    fi
  else
    if ! clang-format --style="file:${FORMAT_FILE}" \
      -i "${file}"; then
      ((failed += 1))
      if [ -t 1 ]; then
        printf '\r\033[2K'
      fi
      printf '%s✗%s %s\n' "${RED}" "${RESET}" "${relative}"
    fi
  fi

  ((processed += 1))
  render_progress "${processed}" "${relative}"
done

if [ -t 1 ]; then
  printf '\n'
fi

elapsed=$((SECONDS - start))

if [ "${failed}" -gt 0 ]; then
  err "${failed}/${total} files failed (${elapsed}s)"
  exit 1
fi

if "${check}"; then
  printf '%s✓%s All %d files are correctly formatted (%ds).\n' \
    "${GREEN}" "${RESET}" "${total}" "${elapsed}"
else
  printf '%s✓%s Successfully formatted %d files (%ds).\n' \
    "${GREEN}" "${RESET}" "${total}" "${elapsed}"
fi
