#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
cd /data/data/com.termux/files/home/alroot_wayland

for script in termux/alroot-wayland termux/alroot-battery-genmon.sh \
              termux/clipboard-broker.sh termux/awstart; do
    bash -n "$script"
done
python3 - <<'PY'
import xml.etree.ElementTree as ET
from pathlib import Path
profile = Path("/data/data/com.termux/files/home/.alroot-wayland/xfce-config")
root = ET.parse(profile / "xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml").getroot()
plugins = root.find("./property[@name='plugins']")
assert plugins is not None
entries = [p for p in plugins.findall("property") if p.get("value") == "genmon"
           and (p.find("./property[@name='command']") is not None)
           and "alroot-battery-genmon.sh" in p.find("./property[@name='command']").get("value", "")]
assert len(entries) == 1, "missing or duplicated Alroot battery GenMon"
chosen = entries[0].get("name").split("-")[-1]
panels = root.find("./property[@name='panels']")
assert panels is not None
assert any(i.get("value") == chosen for panel in panels.findall("property")
           for i in panel.findall("./property[@name='plugin-ids']/value"))
assert (profile / "autostart/Plank dock.desktop").is_file()
print("PASS: isolated GenMon battery and Plank autostart configuration")
PY
printf '{"percentage":62,"status":"CHARGING","time_ms":1793110000000}\n' \
    > .source/battery-fixture-smoke.json
test_battery="$(ALROOT_BATTERY_CACHE="$PWD/.source/battery-fixture-smoke.json" \
    bash termux/alroot-battery-genmon.sh)"
grep -q '62%' <<< "$test_battery"
echo "PASS: cached Android BatteryManager data renders charging percentage"
if command -v termux-battery-status >/dev/null; then
    result="$(timeout 8 bash termux/alroot-battery-genmon.sh)"
    grep -q '<txt>' <<< "$result"
    echo "Battery widget sample: $(head -1 <<< "$result")"
fi
status="$(awstart --status)"
printf '%s\n' "$status" | grep -E '^host:|^labwc:|^xwayland:|^xfce:|^x11_desktop:|^X DISPLAY:' || true
if grep -q '^xwayland: RUNNING ' <<< "$status"; then
    d="$(sed -n 's/^X DISPLAY: //p' <<< "$status" | head -1)"
    [[ "$d" =~ ^:[0-9]{1,3}$ && "$d" != :0 ]]
    result="$(env -u WAYLAND_DISPLAY DISPLAY="$d" \
        XAUTHORITY="$HOME/.alroot-wayland/Xauthority" xdpyinfo -display "$d")"
    printf '%s\n' "$result" | grep -E 'dimensions:|resolution:' | head -2
    printf '%s\n' "$result" | grep -q 'resolution:[[:space:]]*96x96 dots per inch'
    read -r configured_w configured_h < "$HOME/.alroot-wayland/x11-geometry"
    grep -q "dimensions:[[:space:]]*${configured_w}x${configured_h} pixels" <<< "$result"
    echo "PASS: isolated rootful Xwayland has configured geometry and 96DPI"
fi
echo 'PASS: P02 noninteractive smoke; Android SurfaceView/soft-keyboard still require updated APK test.'
