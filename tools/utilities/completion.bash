#!/usr/bin/env bash

_run_completion()
{
    local tools_dir
    tools_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

    local path="$tools_dir"
    local i

    # Navigate through the command hierarchy
    for ((i=1; i < COMP_CWORD; i++)); do
        path+="/${COMP_WORDS[i]}"
    done

    local current="${COMP_WORDS[COMP_CWORD]}"
    local options=()

    # Find subdirectories
    local entry
    for entry in "$path"/*/; do
        [[ -d "$entry" ]] || continue
        options+=("$(basename "$entry")")
    done

    # Find scripts
    for entry in "$path"/*.sh; do
        [[ -f "$entry" ]] || continue
        options+=("$(basename "$entry" .sh)")
    done

    COMPREPLY=($(compgen -W "${options[*]}" -- "$current"))
}

complete -F _run_completion run