#!/data/data/com.termux/files/usr/bin/bash
# Read-only live Android BatteryManager cache -> Termux battery JSON ABI.
set -euo pipefail
HOME=/data/data/com.termux/files/home
PREFIX=/data/data/com.termux/files/usr
STATE="$HOME/.alroot-wayland"
[[ -r "$STATE/android-battery.json" ]] || exit 2
out="$(env PATH="$STATE/bin:$PREFIX/bin:$PREFIX/bin/applets" \
    timeout 4 "$STATE/bin/termux-battery-status")"
expected="$(jq -r '.percentage' "$STATE/android-battery.json")"
received="$(printf '%s' "$out" | jq -r '.percentage')"
[[ "$expected" =~ ^[0-9]+$ && "$expected" == "$received" ]]
printf '%s\n' "$out" | grep -q '"percentage":[0-9]\+,'
printf '%s\n' "$out" | grep -q '"status":"'
printf '%s\n' "$out" | grep -q '"plugged":"'
original="$(readlink -f "$PREFIX/bin/termux-battery-status")"
[[ "$original" == "$PREFIX/bin/termux-battery-status" ]]
echo "PASS: Termux battery JSON ABI from Android cache $received%, native command untouched"
