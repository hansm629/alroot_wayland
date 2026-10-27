#!/data/data/com.termux/files/usr/bin/bash
# Headless test only, receives a synthetic line in a disposable terminal.
set -euo pipefail
[[ $# -eq 1 ]] || exit 2
IFS= read -r -t 8 input || true
if [[ "$input" == '한글' ]]; then
    printf 'P04_TERMINAL_SYNTHETIC_MATCH=1 chars=%s\n' "${#input}" > "$1"
else
    printf 'P04_TERMINAL_SYNTHETIC_MATCH=0 chars=%s bytes=%s escaped=%q\n' \
        "${#input}" "$(printf '%s' "$input" | wc -c)" "$input" > "$1"
fi
