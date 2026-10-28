#!/data/data/com.termux/files/usr/bin/bash
# Only Alroot Xwayland's XFCE terminal loads patched VTE; never native :0.
set -euo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
STATE="$HOME/.alroot-wayland"
real_terminal="$PREFIX/bin/xfce4-terminal"
private_vte="$STATE/vte-private"
target_display="$(cat "$STATE/display" 2>/dev/null || true)"
if [[ -v DISPLAY && -n "$target_display" && "$target_display" != :0 &&
      ( "$DISPLAY" == "$target_display" ||
        "$DISPLAY" == "$target_display".* ) &&
      "$(pkg-config --modversion vte-2.91 2>/dev/null || true)" == 0.84.1 &&
      -s "$private_vte/libvte-2.91.so.0" ]]; then
    if [[ -v LD_LIBRARY_PATH ]]; then
      export LD_LIBRARY_PATH="$private_vte:$LD_LIBRARY_PATH"
    else
      export LD_LIBRARY_PATH="$private_vte"
    fi
    # XFCE terminal is a D-Bus singleton by default. A pre-P14 server on
    # the same Alroot display can reuse its globally loaded VTE despite a
    # new client's LD_LIBRARY_PATH. Keep that user process untouched;
    # launch a private instance until the old server naturally exits.
    stale_server=false
    for pid in $(ps -A -o pid,args |
                 awk '$2 ~ /\/xfce4-terminal$/ {print $1}'); do
        [[ -r "/proc/$pid/environ" && -r "/proc/$pid/maps" ]] || continue
        pid_display="$(tr '\0' '\n' < "/proc/$pid/environ" |
                       sed -n 's/^DISPLAY=//p' | head -1)"
        [[ "$pid_display" == "$target_display" ||
           "$pid_display" == "$target_display".* ]] || continue
        if grep -q 'libvte-2.91.so.0.8400.1' "/proc/$pid/maps" &&
           ! grep -q '/vte-private/libvte-2.91.so.0' "/proc/$pid/maps"; then
            stale_server=true
            break
        fi
    done
    if [[ "$stale_server" == true ]]; then
        for arg in "$@"; do
            [[ "$arg" != --disable-server ]] || exec "$real_terminal" "$@"
        done
        exec "$real_terminal" --disable-server "$@"
    fi
fi
exec "$real_terminal" "$@"
