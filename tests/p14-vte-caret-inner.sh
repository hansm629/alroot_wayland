#!/data/data/com.termux/files/usr/bin/bash
# Only invoked within the isolated Xvfb/DBus P14 test.
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
PREFIX=/data/data/com.termux/files/usr
STATE=/data/data/com.termux/files/home/.alroot-wayland
WORK="$1"
cd "$ROOT"
fcitx_pid="" native_pid="" gui_pid="" sender_pid=""
cleanup() {
    for pid in "$sender_pid" "$gui_pid" "$native_pid" "$fcitx_pid"; do
        [[ -z "$pid" ]] || kill -TERM "$pid" 2>/dev/null || true
    done
    [[ -z "$fcitx_pid" ]] || wait "$fcitx_pid" 2>/dev/null || true
    [[ -z "$native_pid" ]] || wait "$native_pid" 2>/dev/null || true
}
trap cleanup EXIT
fcitx5 > "$WORK/fcitx.log" 2>&1 &
fcitx_pid=$!
"$ROOT/packages/bin/alroot-ime-server" "$WORK/ime.sock" \
    > "$WORK/ime-server.log" 2>&1 &
native_pid=$!
for i in $(seq 1 50); do
    [[ -S "$WORK/fcitx.sock" && -S "$WORK/ime.sock" ]] && break
    sleep .1
done
[[ -S "$WORK/fcitx.sock" && -S "$WORK/ime.sock" ]] || {
    tail -16 "$WORK/fcitx.log" "$WORK/ime-server.log" >&2
    exit 2
}
for variant in original patched; do
    if [[ "$variant" == original ]]; then
        unset LD_LIBRARY_PATH
    else
        export LD_LIBRARY_PATH="$STATE/vte-private:$PREFIX/lib"
    fi
    "$WORK/probe" > "$WORK/$variant-gtk.log" 2>&1 &
    gui_pid=$!
    window=""
    for i in $(seq 1 30); do
        window="$(xdotool search --name 'Alroot P14 private VTE cursor' |
                  tail -1 || true)"
        [[ -n "$window" ]] && break
        sleep .1
    done
    [[ -n "$window" ]] || { echo "GTK probe window missing" >&2; exit 2; }
    xdotool windowfocus "$window"
    [[ "$(xdotool getwindowfocus)" == "$window" ]]
    sleep .2
    python3 - "$WORK/ime.sock" <<'PY' &
import socket, struct, sys, time
sock = socket.socket(socket.AF_UNIX)
sock.settimeout(2)
sock.connect(sys.argv[1])
value = "한".encode("utf-8")
def send(op):
    sock.sendall(op.encode("ascii") + struct.pack("!I", len(value)) + value)
send("C")
time.sleep(.78)
send("T")
time.sleep(.60)
sock.close()
PY
    sender_pid=$!
    sleep .35
    import -window "$window" "$WORK/$variant-preedit.png"
    sleep .89
    import -window "$window" "$WORK/$variant-committed.png"
    wait "$sender_pid"
    sender_pid=""
    wait "$gui_pid"
    gui_pid=""
    grep -q 'P14_VTE_VISUAL_PROBE_FINISHED' "$WORK/$variant-gtk.log"
done
echo 'PASS: original and patched VTE captured on disposable Xvfb'
