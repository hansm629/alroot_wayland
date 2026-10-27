#!/data/data/com.termux/files/usr/bin/bash
# Non-destructive diagnostics for the four P03 fundamentals.
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
STATE=/data/data/com.termux/files/home/.alroot-wayland
cd "$ROOT"
for sh in termux/alroot-wayland termux/aw-proot termux/shims/xhost \
          termux/alroot-battery-genmon.sh tools/install-alroot-proot-shims.sh; do
    bash -n "$sh"
done
echo "PASS: shell syntax"

python3 - <<'PY'
from pathlib import Path
import xml.etree.ElementTree as ET
p=Path('/data/data/com.termux/files/home/.alroot-wayland/xfce-config/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml')
r=ET.parse(p).getroot()
plugin=r.find("./property[@name='plugins']/property[@name='plugin-107']")
if plugin is None:
    plugins=r.find("./property[@name='plugins']")
    plugin=next((e for e in plugins if e.get('value')=='genmon'),None)
assert plugin is not None and plugin.get('value')=='genmon'
assert plugin.find("./property[@name='use-label']").get('value')=='true'
assert plugin.find("./property[@name='text']").get('value')=='BAT'
print('PASS: project-only XFCE battery label is explicitly visible')
PY
bat="$(bash termux/alroot-battery-genmon.sh)"
printf 'Battery: %s\n' "$(head -1 <<< "$bat")"

status="$(awstart --status)"
printf '%s\n' "$status" | grep -E '^host:|^labwc:|^xwayland:|^xfce:|^x11_desktop:|^X DISPLAY:'

if grep -q '^xwayland: RUNNING ' <<< "$status"; then
    display="$(cat "$STATE/display")"
    [[ "$display" =~ ^:[0-9]{1,3}$ && "$display" != :0 ]]
    export DISPLAY="$display" XAUTHORITY="$STATE/Xauthority"
    printf '\nProject Xwayland current refresh (this is not the physical-rate proof):\n'
    xrandr --query | head -8
    printf '\nPRoot connectivity (three wrappers, separate from Termux-X11 :0):\n'
    for runner in aw-mpd aw-mprun aw-pdrun; do
        # Never pass -display here: it would conceal a login shell silently
        # rewriting DISPLAY back to the original Termux-X11 :0.
        result="$(timeout 14 "$runner" /usr/bin/xdpyinfo 2>&1)" || {
            echo "FAIL: $runner cannot reach isolated $display" >&2
            exit 1
        }
        grep -q "name of display: *$display" <<< "$result"
        echo "PASS: $runner → Alroot $display with explicit Xauthority"
    done
fi
echo 'Recent Android timing evidence (do not infer unsupported refresh):'
logcat -d -s AlrootWayland 2>/dev/null | grep 'DisplayTiming' | tail -9 || true
echo 'PASS: diagnostics. Physical 120Hz and on-device Android IME remain pending visual tests.'
