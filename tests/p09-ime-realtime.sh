#!/data/data/com.termux/files/usr/bin/bash
# Only on a disposable authenticated Xvfb, never user's Termux-X11 :0/Alroot :90.
set -euo pipefail
HOME=/data/data/com.termux/files/home
PREFIX=/data/data/com.termux/files/usr
ROOT="$HOME/alroot_wayland"
cd "$ROOT"
mkdir -p .source
work="$(mktemp -d "$ROOT/.source/p09-live.XXXXXXXX")"
chmod 700 "$work"
xvfbpid=""; imepid=""; gtkpid=""
cleanup() {
    for pid in "$gtkpid" "$imepid" "$xvfbpid"; do
        [[ -z "$pid" ]] || kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT
display=""
for n in 196 197 198 199 200; do
    if [[ ! -S "$PREFIX/tmp/.X11-unix/X$n" ]]; then display=":$n"; break; fi
done
[[ -n "$display" ]] || exit 2
touch "$work/Xauthority"
chmod 600 "$work/Xauthority"
cookie="$(openssl rand -hex 16)"
xauth -f "$work/Xauthority" add "$display" MIT-MAGIC-COOKIE-1 "$cookie"
unset cookie
Xvfb "$display" -screen 0 960x540x24 -auth "$work/Xauthority" -nolisten tcp \
    > "$work/xvfb.log" 2>&1 &
xvfbpid=$!
sleep .4
bash termux/build-ime-server.sh > "$work/native-build.log" 2>&1
env DISPLAY="$display" XAUTHORITY="$work/Xauthority" \
    "$ROOT/packages/bin/alroot-ime-server" "$work/ime.sock" \
    > "$work/ime-native.log" 2>&1 &
imepid=$!
sleep .3
[[ -S "$work/ime.sock" ]] || { cat "$work/ime-native.log"; exit 1; }
clang -O1 tests/p09-ime-gtk-probe.c -o "$work/gtk" \
    $(pkg-config --cflags --libs gtk+-3.0)
mkdir -p "$work/jvm"
javac -encoding UTF-8 -d "$work/jvm" \
    app/src/main/java/com/alroot/wayland/ImeCommitState.java \
    tests/ImeRealtimeBridgeProbe.java
env DISPLAY="$display" XAUTHORITY="$work/Xauthority" GDK_BACKEND=x11 \
    "$work/gtk" > "$work/gtk.log" 2>&1 &
gtkpid=$!
sleep .6
java -cp "$work/jvm" ImeRealtimeBridgeProbe "$work/ime.sock" \
    > "$work/java.log" 2>&1
wait "$gtkpid"
gtkpid=""
grep -q 'P09_X11_GTK_MATCH=1' "$work/gtk.log" || {
    cat "$work/gtk.log" "$work/java.log"; exit 1;
}
java_us="$(sed -n 's/^FIRST_HANGUL_PREEDIT_SEND_US=//p' "$work/java.log")"
gtk_us="$(sed -n 's/^FIRST_HANGUL_VISIBLE_US=//p' "$work/gtk.log")"
[[ "$java_us" =~ ^[0-9]+$ && "$gtk_us" =~ ^[0-9]+$ ]]
delay_ms="$(python3 - "$java_us" "$gtk_us" <<'PY'
import sys
delta = (int(sys.argv[2])-int(sys.argv[1]))/1000
assert 0 <= delta < 400, 'Native XTEST preedit latency unexpectedly high'
print(round(delta,2))
PY
)"
printf 'PASS: Native XTEST first Hangul visible after %s ms (synthetic Java send → GTK)\n' "$delay_ms"
grep '^P09_X11_GTK_MATCH=' "$work/gtk.log"
grep '^REALTIME_STREAM_' "$work/java.log"
echo 'PASS: English→Hangul C/T and Hangul Backspace; no Android keyboard acceptance implied'
