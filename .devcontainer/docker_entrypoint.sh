#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$(realpath "$SCRIPT_DIR/../tools")"
BASHRC="$HOME/.bashrc"

# Make all tools executable recursively
find "$TOOLS_DIR" -type f \( -name "*.sh" -o -name "run" \) -exec chmod +x {} +

# Remove previous ESP-IDF initialization entries
sed -i '\|/opt/esp/idf/export.sh|d' "$BASHRC"

# Configure tools PATH
PATH_LINE="export PATH=\"$TOOLS_DIR:\$PATH\""

grep -qxF "$PATH_LINE" "$BASHRC" ||
    echo "$PATH_LINE" >> "$BASHRC"

# Activate ESP-IDF silently, keeping stderr visible
IDF_INIT='[[ -n "${IDF_PYTHON_ENV_PATH:-}" ]] || source /opt/esp/idf/export.sh > /dev/null'

echo "$IDF_INIT" >> "$BASHRC"

# Enable Bash autocompletion
sed -i '\|/tools/utilities/completion.bash|d' "$BASHRC"
COMPLETION_FILE="$TOOLS_DIR/.utilities/completion.bash"
COMPLETION_LINE="source \"$COMPLETION_FILE\""

grep -qxF "$COMPLETION_LINE" "$BASHRC" ||
    echo "$COMPLETION_LINE" >> "$BASHRC"
