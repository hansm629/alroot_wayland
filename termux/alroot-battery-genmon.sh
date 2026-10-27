#!/data/data/com.termux/files/usr/bin/bash
# XFCE GenMon battery widget for Alroot's isolated X11 panel.
# Prefer Android BatteryManager snapshot from the helper foreground service;
# Termux:API direct calls from GTK panel workers frequently time out.
set -uo pipefail
export PATH=/data/data/com.termux/files/usr/bin:/data/data/com.termux/files/usr/bin/applets

cache="${ALROOT_BATTERY_CACHE:-/data/data/com.termux/files/home/.alroot-wayland/android-battery.json}"
json=""
if [[ -r "$cache" ]]; then
    now="$(date +%s)"
    modified="$(stat -c %Y "$cache" 2>/dev/null || echo 0)"
    if [[ "$modified" =~ ^[0-9]+$ ]] && ((now >= modified && now - modified < 180)); then
        json="$(head -c 2048 "$cache")"
    fi
fi
if [[ -z "$json" ]]; then
    json="$(timeout 3 termux-battery-status 2>/dev/null || true)"
fi

percentage="$(printf '%s' "$json" | jq -r '.percentage // .level // empty' 2>/dev/null)"
status="$(printf '%s' "$json" | jq -r '.status // "UNKNOWN"' 2>/dev/null)"
plugged="$(printf '%s' "$json" | jq -r '.plugged // "UNKNOWN"' 2>/dev/null)"
if [[ "$percentage" =~ ^[0-9]{1,3}$ ]] && ((percentage <= 100)); then
    if [[ "$status" == CHARGING || "$status" == FULL ||
          ( "$plugged" != UNKNOWN && "$plugged" != UNPLUGGED ) ]]; then
        text="⚡ ${percentage}%"
    else
        text="🔋 ${percentage}%"
    fi
    printf '<txt>%s</txt>\n' "$text"
    printf '<tool>Android battery: %s%% | %s</tool>\n' "$percentage" "$status"
else
    printf '<txt>BAT --%%</txt>\n<tool>Android battery cache unavailable</tool>\n'
fi
