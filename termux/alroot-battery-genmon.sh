#!/data/data/com.termux/files/usr/bin/bash
# XFCE GenMon battery widget for isolated Alroot X11 panel.
set -uo pipefail
export PATH=/data/data/com.termux/files/usr/bin:/data/data/com.termux/files/usr/bin/applets
json="$(timeout 5 termux-battery-status 2>/dev/null || true)"
percentage="$(printf '%s' "$json" | jq -r '.percentage // .level // empty' 2>/dev/null)"
plugged="$(printf '%s' "$json" | jq -r '.plugged // "UNKNOWN"' 2>/dev/null)"
status="$(printf '%s' "$json" | jq -r '.status // "UNKNOWN"' 2>/dev/null)"
if [[ "$percentage" =~ ^[0-9]{1,3}$ ]] && ((percentage <= 100)); then
    if [[ "$status" == CHARGING || "$plugged" != UNPLUGGED ]]; then
        text="⚡ ${percentage}%"
    else
        text="🔋 ${percentage}%"
    fi
    printf '<txt>%s</txt>\n' "$text"
    printf '<tool>Android battery: %s%% | %s | %s</tool>\n' "$percentage" "$status" "$plugged"
else
    printf '<txt>BAT --%%</txt>\n<tool>Termux:API battery temporarily unavailable</tool>\n'
fi
