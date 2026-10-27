#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

STATE="$1"
DISPLAY="$2"
export DISPLAY XAUTHORITY="$STATE/Xauthority"
export PATH="/data/data/com.termux/files/usr/bin:/data/data/com.termux/files/usr/bin/applets"
export TMPDIR="/data/data/com.termux/files/usr/tmp"
IPC="$STATE/ipc"
mkdir -p "$IPC"
chmod 700 "$IPC"
umask 077

ANDROID="$IPC/android-to-x11.txt"
LINUX="$IPC/x11-to-android.txt"
LAST="$IPC/.last-injected.txt"
MAX_BYTES=1048576
IME="$IPC/ime-requests"
mkdir -p "$IME"
chmod 700 "$IME"

while :; do
    # Route completed Android IME text into this session's rootful Xwayland.
    # Preedit stays Android-side to avoid duplicate Hangul syllables.
    for request in "$IME"/[0-9]*; do
        [[ -f "$request" ]] || continue
        case "$request" in
            *.commit)
                if command -v xdotool >/dev/null 2>&1; then
                    timeout 10 xdotool type --clearmodifiers --delay 0 --file "$request" \
                        >/dev/null 2>&1 || echo 'IME commit delivery failed' >&2
                else
                    echo 'IME unavailable: xdotool missing' >&2
                fi
                ;;
            *.delete)
                before=0; after=0
                read -r before after < "$request" || true
                if [[ "${before:-}" =~ ^[0-9]+$ && "${after:-}" =~ ^[0-9]+$ ]]; then
                    for ((i=0; i<before && i<128; ++i)); do
                        xdotool key --clearmodifiers BackSpace >/dev/null 2>&1 || break
                    done
                    for ((i=0; i<after && i<128; ++i)); do
                        xdotool key --clearmodifiers Delete >/dev/null 2>&1 || break
                    done
                fi
                ;;
        esac
        rm -f "$request"
    done
    if [[ -f "$ANDROID" ]]; then
        if [[ ! -f "$LAST" ]] || ! cmp -s "$ANDROID" "$LAST"; then
            bytes="$(stat -c %s "$ANDROID" 2>/dev/null || echo 99999999)"
            if (( bytes <= MAX_BYTES )); then
                if timeout 2 xclip -selection clipboard -in < "$ANDROID" 2>/dev/null; then
                    cp -f "$ANDROID" "$LAST"
                fi
            fi
        fi
    fi

    # Inspect only this session's X DISPLAY. Do not execute clipboard content.
    temporary="$(mktemp "$IPC/.read.XXXXXXXX")"
    if timeout 2 xclip -selection clipboard -out -t UTF8_STRING > "$temporary" 2>/dev/null; then
        bytes="$(stat -c %s "$temporary")"
        if (( bytes <= MAX_BYTES )); then
            if [[ -f "$ANDROID" ]] && cmp -s "$ANDROID" "$temporary"; then
                :
            elif [[ ! -f "$LINUX" ]] || ! cmp -s "$LINUX" "$temporary"; then
                chmod 600 "$temporary"
                mv -f "$temporary" "$LINUX"
                temporary=""
            fi
        fi
    fi
    [[ -z "$temporary" ]] || rm -f "$temporary"
    sleep .5
done
