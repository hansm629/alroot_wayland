#!/data/data/com.termux/files/usr/bin/bash
# Confirm the native XFCE plugin's own D-Bus system address sees the
# Android battery cache. Does not call or modify original Termux-X11.
set -euo pipefail
HOME=/data/data/com.termux/files/home
STATE="$HOME/.alroot-wayland"
PREFIX=/data/data/com.termux/files/usr
[[ -r "$STATE/android-battery.json" ]] || {
    echo 'No fresh Android helper battery snapshot' >&2
    exit 2
}
expected="$("$PREFIX/bin/jq" -r '.percentage' "$STATE/android-battery.json")"
[[ "$expected" =~ ^[0-9]+$ ]] || exit 2

pid=""
for p in $(ps -A -o pid,args | awk '/libbattery.so 7 / {print $1}'); do
    [[ -r "/proc/$p/environ" ]] || continue
    if tr '\0' '\n' < "/proc/$p/environ" | grep -qx 'DISPLAY=:90'; then
        pid="$p"
        break
    fi
done
[[ -n "$pid" ]] || {
    echo 'No native XFCE battery plugin on Alroot display :90' >&2
    exit 2
}
address="$(tr '\0' '\n' < "/proc/$pid/environ" |
    sed -n 's/^DBUS_SYSTEM_BUS_ADDRESS=//p' | head -1)"
[[ "$address" == unix:* ]] || {
    echo 'Alroot battery plugin did not inherit its private system bus' >&2
    exit 2
}
actual="$(DBUS_SESSION_BUS_ADDRESS="$address" timeout 6 "$PREFIX/bin/gdbus" call \
    --session --dest org.freedesktop.UPower \
    --object-path /org/freedesktop/UPower/devices/battery_BAT0 \
    --method org.freedesktop.DBus.Properties.Get \
    org.freedesktop.UPower.Device Percentage)"
python3 - "$actual" "$expected" <<'PY'
import re,sys
match=re.search(r'<([0-9]+(?:\.[0-9]+)?)>',sys.argv[1])
assert match is not None, 'UPower Percentage property missing'
assert abs(float(match.group(1))-float(sys.argv[2]))<1.0, 'Stale percentage'
print('PASS: native XFCE plugin private UPower Percentage matches Android cache')
PY
