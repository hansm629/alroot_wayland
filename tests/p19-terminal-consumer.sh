#!/data/data/com.termux/files/usr/bin/bash
# Consume exactly one XFCE/VTE line on an isolated Xvfb, never :0/:90.
set -uo pipefail
if IFS= read -r -t 10 input; then
    if [[ "$input" == "hello" ]]; then
        echo "P19_VTE_ENGLISH_HANGUL_DEL_MATCH=1" > "$1"
    else
        # Do not retain or echo private text even in the test logs.
        echo "P19_VTE_ENGLISH_HANGUL_DEL_MATCH=0 chars=${#input}" > "$1"
    fi
else
    echo "P19_VTE_ENGLISH_HANGUL_DEL_MATCH=0 timeout" > "$1"
fi
