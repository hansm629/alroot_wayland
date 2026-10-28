#!/data/data/com.termux/files/usr/bin/bash
# Project-only replacement for the command spawned by Termux's
# xfce4-battery-plugin/libacpi.c. Uses Android BatteryManager cache,
# avoiding the background Termux:API timeout that leaves the plugin '?'.
# This MUST be found only through Alroot's isolated XFCE PATH.
set -euo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
CACHE="$HOME/.alroot-wayland/android-battery.json"

if [[ -r "$CACHE" ]]; then
    now="$(date +%s)"
    mtime="$(stat -c %Y "$CACHE" 2>/dev/null || echo 0)"
    if [[ "$mtime" =~ ^[0-9]+$ ]] &&
            ((now >= mtime && now-mtime < 180)); then
        result="$("$PREFIX/bin/jq" -ecr '
            if (.percentage | type) == "number" and
               .percentage >= 0 and .percentage <= 100 then
                {
                    present: true,
                    percentage: (.percentage | floor),
                    level: (.percentage | floor),
                    scale: 100,
                    plugged: (if .status == "CHARGING" or .status == "FULL"
                              then "PLUGGED" else "UNPLUGGED" end),
                    status: (.status // "UNKNOWN"),
                    temperature: (.temperature // 0.0)
                }
            else empty end
        ' "$CACHE" 2>/dev/null)" || result=""
        if [[ -n "$result" ]]; then
            printf '%s\n' "$result"
            exit 0
        fi
    fi
fi
# Scoped fallback for a helper that has not yet delivered its initial
# BatteryManager snapshot. Do not recursively call the local shim.
exec timeout 4 "$PREFIX/bin/termux-battery-status"
