#!/data/data/com.termux/files/usr/bin/bash
# Rapid distinct Korean glyph regression on isolated Xvfb (never :0/:90).
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
PREFIX=/data/data/com.termux/files/usr
cd "$ROOT"
mkdir -p .source
work="$(mktemp -d "$PWD/.source/p06-regression.XXXXXXXX")"
chmod 700 "$work"
gtkpid=""; imepid=""; xvfbpid=""
cleanup() {
    for p in "$gtkpid" "$imepid" "$xvfbpid"; do
        [[ -z "$p" ]] || kill -TERM "$p" 2>/dev/null || true
    done
}
trap cleanup EXIT
display=""
for n in 195 196 197 198 199 200; do
    if [[ ! -S "$PREFIX/tmp/.X11-unix/X$n" ]]; then
        display=":$n"
        break
    fi
done
[[ -n "$display" ]] || exit 2
touch "$work/Xauthority"
chmod 600 "$work/Xauthority"
cookie="$(openssl rand -hex 16)"
xauth -f "$work/Xauthority" add "$display" MIT-MAGIC-COOKIE-1 "$cookie"
unset cookie
Xvfb "$display" -screen 0 960x540x24 -auth "$work/Xauthority" \
    -nolisten tcp > "$work/xvfb.log" 2>&1 &
xvfbpid=$!
sleep .4
"$PREFIX/bin/bash" termux/build-ime-server.sh >/dev/null
env DISPLAY="$display" XAUTHORITY="$work/Xauthority" \
    "$ROOT/packages/bin/alroot-ime-server" "$work/ime.sock" \
    > "$work/ime.log" 2>&1 &
imepid=$!
sleep .3
clang -O1 tests/p06-ime-distinct-gtk.c -o "$work/gtk" \
    $(pkg-config --cflags --libs gtk+-3.0)
for count in 10 24; do
    expected="$(python3 - "$count" <<'PY'
import sys
print(''.join(chr(0xac00+i) for i in range(int(sys.argv[1]))),end='')
PY
)"
    env DISPLAY="$display" XAUTHORITY="$work/Xauthority" GDK_BACKEND=x11 \
        "$work/gtk" "$expected" > "$work/gtk-$count.log" 2>&1 &
    gtkpid=$!
    sleep .65
    python3 - "$work/ime.sock" "$count" <<'PY'
import socket,struct,sys
sock=socket.socket(socket.AF_UNIX)
sock.connect(sys.argv[1])
for i in range(int(sys.argv[2])):
    char=chr(0xac00+i)
    for op,text in (('C',char),('T',char),('F','')):
        payload=text.encode('utf-8')
        sock.sendall(op.encode()+struct.pack('!I',len(payload))+payload)
sock.close()
PY
    wait "$gtkpid"
    gtkpid=""
    grep -q "^P06_GUI_EXPECTED_MATCH=1 expected_chars=$count actual_chars=$count" \
        "$work/gtk-$count.log" || {
        cat "$work/gtk-$count.log"
        echo "FAIL: $count distinct Korean syllables mismatch" >&2
        exit 1
    }
    echo "PASS: $count distinct Korean syllables rapidly committed in Xvfb"
done
echo "PASS: reserved keycode pool prevents previous-glyph replacement"
