#!/data/data/com.termux/files/usr/bin/bash
set -uo pipefail
if IFS= read -r -t 10 input; then
  [[ "$input" == "한글" ]] && printf 'PASS_NATIVE_FCITX_TERMINAL=1\n' > "$1" ||
      printf 'PASS_NATIVE_FCITX_TERMINAL=0 characters=%s\n' "${#input}" > "$1"
else
  printf 'PASS_NATIVE_FCITX_TERMINAL=0 timeout\n' > "$1"
fi
