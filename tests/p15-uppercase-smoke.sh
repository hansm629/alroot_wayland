#!/data/data/com.termux/files/usr/bin/bash
# P15: uppercase + punctuation through real XTEST -> GTK, only on private Xvfb.
# Never inject synthetic keys into the user's active :0 / Alroot :90 desktop.
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
PREFIX=/data/data/com.termux/files/usr
cd "$ROOT"
mkdir -p .source/p15-tests
work="$(mktemp -d "$ROOT/.source/p15-tests/uppercase.XXXXXXXX")"
chmod 700 "$work"
display=""
for n in 220 221 222 223 224; do
    if [[ ! -S "$PREFIX/tmp/.X11-unix/X$n" ]]; then
        display=":$n"
        break
    fi
done
[[ -n "$display" ]] || { echo 'No free private Xvfb display' >&2; exit 2; }
xvfb_pid="" native_pid="" gtk_pid=""
cleanup() {
    for pid in "$gtk_pid" "$native_pid" "$xvfb_pid"; do
        [[ -z "$pid" ]] || kill -TERM "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT
bash termux/build-ime-server.sh
clang -O1 tests/p11-native-edit-gtk.c -o "$work/gtk" \
    $(pkg-config --cflags --libs gtk+-3.0)
javac -encoding UTF-8 -d "$work" \
    app/src/main/java/com/alroot/wayland/ImeVirtualShiftPolicy.java \
    tests/ImeVirtualShiftPolicyTest.java
java -cp "$work" com.alroot.wayland.ImeVirtualShiftPolicyTest

export DISPLAY="$display" XAUTHORITY="$work/Xauthority"
export XDG_RUNTIME_DIR="$work/runtime" XDG_CONFIG_HOME="$work/config"
export GTK_IM_MODULE=xim
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"
chmod 700 "$XDG_RUNTIME_DIR"
touch "$XAUTHORITY"; chmod 600 "$XAUTHORITY"
xauth -f "$XAUTHORITY" add "$display" MIT-MAGIC-COOKIE-1 \
    "$(openssl rand -hex 16)"
Xvfb "$display" -screen 0 900x300x24 -auth "$XAUTHORITY" -nolisten tcp \
    > "$work/xvfb.log" 2>&1 & xvfb_pid=$!
for i in $(seq 1 40); do
    [[ -S "$PREFIX/tmp/.X11-unix/X${display#:}" ]] && break
    sleep .05
done
[[ -S "$PREFIX/tmp/.X11-unix/X${display#:}" ]]
"$ROOT/packages/bin/alroot-ime-server" "$work/ime.sock" \
    > "$work/ime.log" 2>&1 & native_pid=$!
for i in $(seq 1 40); do
    [[ -S "$work/ime.sock" ]] && break
    sleep .05
done
[[ -S "$work/ime.sock" ]]
"$work/gtk" 'aAZz!?' > "$work/gtk.log" 2>&1 & gtk_pid=$!
window=""
for i in $(seq 1 30); do
    window="$(xdotool search --name 'Alroot P11 native GTK edit test' |
              tail -1 || true)"
    [[ -n "$window" ]] && break
    sleep .05
done
[[ -n "$window" ]] || { echo 'GTK input window missing' >&2; exit 2; }
xdotool windowfocus "$window"
sleep .1
python3 - "$work/ime.sock" <<'PY'
import socket, struct, sys, time
data = 'aAZz!?'.encode('utf-8')
sock = socket.socket(socket.AF_UNIX)
sock.connect(sys.argv[1])
for op in ('C', 'T'):
    sock.sendall(op.encode('ascii') + struct.pack('!I', len(data)) + data)
    time.sleep(.1)
sock.close()
PY
wait "$gtk_pid"
gtk_pid=""
cat "$work/gtk.log"
grep -q 'P11_NATIVE_EDIT_MATCH=1' "$work/gtk.log"
echo 'PASS: aAZz!? is preserved by the native XTEST bridge in isolated GTK'
echo 'NOTE: Samsung physical soft-keyboard uppercase remains user TEST-PENDING'
