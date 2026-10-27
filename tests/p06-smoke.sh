#!/data/data/com.termux/files/usr/bin/bash
# P06 read-only checks against the isolated Alroot X11 server.
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
cd "$ROOT"
bash tests/p05-smoke.sh
python3 -m py_compile termux/alroot-refresh-sync.py termux/configure-fastfetch.py
source_display="$(cat "$HOME/.alroot-wayland/display")"
[[ "$source_display" =~ ^:[0-9]{1,3}$ && "$source_display" != :0 ]]
mode="$(DISPLAY="$source_display" \
    XAUTHORITY="$HOME/.alroot-wayland/Xauthority" xrandr --current 2>/dev/null)"
grep -q '_120Hz.*119[.]' <<< "$mode" || {
    echo "NOTE: selected mode is not ~120Hz; check Android physical refresh" >&2
}
text="$(aw-debian fastfetch --pipe)"
display_line="$(grep 'Display:' <<< "$text" | head -1)"
[[ -n "$display_line" ]]
[[ "$display_line" != *XWAYLAND* && "$display_line" != *Alroot* ]]
echo "PASS: Alroot Debian fastfetch uses a neutral Display label"
echo "$display_line"
bash tests/p06-ime-distinct.sh
echo 'PASS: P06 smoke. Physical multitouch and Samsung IME remain on-device tests.'
