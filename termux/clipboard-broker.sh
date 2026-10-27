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

ime_paste_utf8() {
    local request="$1" saved="" restored=0 shortcut="ctrl+v" classname=""
    saved="$(mktemp "$IPC/.ime-clip-XXXXXXXX")" || return 1
    if timeout 2 xclip -selection clipboard -out -t UTF8_STRING > "$saved" 2>/dev/null; then
        restored=1
    fi
    if ! timeout 3 xclip -selection clipboard -in -t UTF8_STRING < "$request" 2>/dev/null; then
        rm -f "$saved"
        echo 'IME Unicode clipboard selection failed' >&2
        return 1
    fi
    classname="$(timeout 2 xdotool getwindowfocus getwindowclassname 2>/dev/null || true)"
    case "$classname" in
        *[Tt]erminal*|*XTerm*|*xterm*|*Konsole*|*konsole*)
            shortcut="ctrl+shift+v" ;;
    esac
    local code=0
    timeout 3 xdotool key --clearmodifiers "$shortcut" >/dev/null 2>&1 || code=1
    # X11 selection requests are asynchronous; do not restore before the
    # destination receives the UTF-8 text. The old CLIPBOARD is then restored.
    sleep .35
    if ((restored)); then
        timeout 3 xclip -selection clipboard -in -t UTF8_STRING < "$saved" 2>/dev/null || true
    else
        timeout 3 xclip -selection clipboard -in -t UTF8_STRING < /dev/null 2>/dev/null || true
    fi
    rm -f "$saved"
    return "$code"
}

while :; do
    # Route completed Android IME text into this session's rootful Xwayland.
    # Preedit stays Android-side to avoid duplicate Hangul syllables.
    for request in "$IME"/[0-9]*; do
        [[ -f "$request" ]] || continue
        case "$request" in
            *.commit)
                bytes="$(stat -c %s "$request" 2>/dev/null || echo 0)"
                if ! [[ "$bytes" =~ ^[0-9]+$ ]] || ((bytes < 1 || bytes > 16384)); then
                    echo "IME rejected invalid commit size" >&2
                    rm -f "$request"
                    continue
                fi
                if command -v xdotool >/dev/null 2>&1; then
                    if LC_ALL=C grep -q '[^[:print:][:space:]]' "$request"; then
                        if ime_paste_utf8 "$request"; then
                            echo "IME Unicode paste delivered bytes=$bytes" >&2
                        else
                            echo "IME Unicode paste failed bytes=$bytes" >&2
                        fi
                    elif timeout 10 xdotool type --clearmodifiers --delay 0 --file "$request" \
                            >/dev/null 2>&1; then
                        echo "IME ASCII XTEST delivered bytes=$bytes" >&2
                    else
                        echo "IME XTEST delivery failed bytes=$bytes" >&2
                    fi
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
    # The persistent native IME server temporarily owns the X11 clipboard
    # while delivering Unicode text. Never echo a transient syllable into
    # the Android clipboard or overwrite the IME server's selection.
    if [[ -e "$IPC/ime.sock.active" ]]; then
        sleep .02
        continue
    fi
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
