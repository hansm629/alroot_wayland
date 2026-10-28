#!/data/data/com.termux/files/usr/bin/bash
# P11 project-only private Xvfb, D-Bus, Fcitx5 and legacy XTEST comparison.
set -euo pipefail
PROJECT=/data/data/com.termux/files/home/alroot_wayland
PREFIX=/data/data/com.termux/files/usr
base="$PROJECT/.source/p11-fcitx-probe"
export base
cd "$PROJECT"
mkdir -p "$base/test" "$base/test/runtime" "$base/config" "$base/addon" "$base/fcitx-only-data/addon"
chmod 700 "$base/test" "$base/test/runtime"
cp -f termux/alroot-fcitx-addon.conf "$base/fcitx-only-data/addon/alrootbridge.conf"
clang++ -std=c++20 -fPIC -shared -O2 -Wall -Wextra -Wno-unused-parameter \
    termux/alroot-fcitx-bridge.cpp \
    -o "$base/addon/libalrootbridge.so" \
    $(pkg-config --cflags --libs Fcitx5Core Fcitx5Utils Fcitx5Config)
bash termux/build-ime-server.sh > "$base/test/build-native.log" 2>&1
clang -O1 tests/p11-native-gtk.c \
    -o "$base/gtk-preedit-probe" $(pkg-config --cflags --libs gtk+-3.0)
clang -O1 tests/p11-native-edit-gtk.c \
    -o "$base/gtk-edit-probe" $(pkg-config --cflags --libs gtk+-3.0)
clang -O1 -Wno-deprecated-declarations tests/p13-gtk-caret.c \
    -o "$base/gtk-caret-probe" $(pkg-config --cflags --libs gtk+-3.0)
clang -O1 tests/p11-raw-xlib.c -o "$base/xlib-fallback" -lX11
mkdir -p "$base/jvm"
javac -encoding UTF-8 -d "$base/jvm" \
    app/src/main/java/com/alroot/wayland/ImeCommitState.java \
    app/src/main/java/com/alroot/wayland/ImeCursorTracker.java \
    tests/ImeEnglishEchoNativeProbe.java
display=""
for n in 206 207 208 209 210; do
    [[ ! -S "$PREFIX/tmp/.X11-unix/X$n" ]] && { display=":$n"; break; }
done
[[ -n "$display" ]] || exit 2
auth="$base/test/Xauthority"
touch "$auth"
chmod 600 "$auth"
xauth -f "$auth" add "$display" MIT-MAGIC-COOKIE-1 "$(openssl rand -hex 16)"
Xvfb "$display" -screen 0 960x540x24 -auth "$auth" -nolisten tcp \
    > "$base/test/xvfb.log" 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep .45
export DISPLAY="$display" XAUTHORITY="$auth"
export XDG_CONFIG_HOME="$base/config"
export XDG_RUNTIME_DIR="$base/test/runtime"
export FCITX_DATA_HOME="$base/fcitx-only-data"
export FCITX_ADDON_DIRS="$base/addon:$PREFIX/lib/fcitx5"
export ALROOT_FCITX_BRIDGE_SOCKET="$base/test/fcitx.sock"
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
dbus-run-session -- bash -c '
set -euo pipefail
fcitx5 > "$XDG_RUNTIME_DIR/fcitx.log" 2>&1 & fcitx_pid=$!
"$HOME/alroot_wayland/packages/bin/alroot-ime-server" "$XDG_RUNTIME_DIR/ime.sock" \
    > "$XDG_RUNTIME_DIR/native.log" 2>&1 & native_pid=$!
gui_pid=""
cleanup(){
    [[ -z "$gui_pid" ]] || kill "$gui_pid" 2>/dev/null || true
    kill -TERM "$fcitx_pid" "$native_pid" 2>/dev/null || true
    wait "$native_pid" "$fcitx_pid" 2>/dev/null || true
}
trap cleanup EXIT
for i in $(seq 1 50); do
    [[ -S "$ALROOT_FCITX_BRIDGE_SOCKET" && -S "$XDG_RUNTIME_DIR/ime.sock" ]] && break
    sleep .1
done
test -S "$ALROOT_FCITX_BRIDGE_SOCKET"
test -S "$XDG_RUNTIME_DIR/ime.sock"
send_series() {
  python3 - "$XDG_RUNTIME_DIR/ime.sock" <<"PY"
import socket,struct,sys,time
s=socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
for op,value in [("C","한"),("C","하"),("T","하")]:
    data=value.encode()
    s.sendall(op.encode()+struct.pack("!I",len(data))+data)
    time.sleep(.085)
s.close()
PY
}
send_ascii() {
  python3 - "$XDG_RUNTIME_DIR/ime.sock" <<"PY"
import socket,struct,sys,time
s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1])
for op,value in (("C","hi"),("T","hi")):
    b=value.encode()
    s.sendall(op.encode()+struct.pack("!I",len(b))+b)
    time.sleep(.05)
s.close()
PY
}
# English must remain the P10 XTEST implementation, regardless of
# whether Fcitx5 could otherwise accept an ASCII client.
$base/xlib-fallback ascii > "$XDG_RUNTIME_DIR/ascii.log" 2>&1 & gui_pid=$!
sleep .60
send_ascii
wait "$gui_pid";gui_pid=""
cat "$XDG_RUNTIME_DIR/ascii.log"
grep -q "ascii_keys=2 success=1" "$XDG_RUNTIME_DIR/ascii.log"
echo "PASS: English typing remains the unchanged XTEST path"
# Raw Xlib has no IM context. It must always use the old XTEST backend,
# even while the Fcitx addon is running.
$base/xlib-fallback > "$XDG_RUNTIME_DIR/raw-before.log" 2>&1 & gui_pid=$!
sleep .60
send_series
wait "$gui_pid";gui_pid=""
cat "$XDG_RUNTIME_DIR/raw-before.log"
grep -q 'success=1' "$XDG_RUNTIME_DIR/raw-before.log"
echo "PASS: Raw Xlib no-IME client uses real XTEST fallback"
# Native Fcitx5 client receives preedit updates without changing real text.
env GTK_IM_MODULE=fcitx GDK_BACKEND=x11 \
 $base/gtk-preedit-probe > "$XDG_RUNTIME_DIR/gtk-native.log" 2>&1 & gui_pid=$!
sleep .65
send_series
wait "$gui_pid";gui_pid=""
cat "$XDG_RUNTIME_DIR/gtk-native.log"
grep -q "GTK_FCITX_FINAL_MATCH=1 text_events=1 preedit_events=3" "$XDG_RUNTIME_DIR/gtk-native.log"
echo "PASS: Native Fcitx GTK preedit rendered, 1 real text mutation"
# Reproducible *visual* proof of the actual GTK preedit appearance:
# the Fcitx addon now requests NoFlag instead of Underline, and the
# native GtkEntry shows the insertion caret at the end of the glyph.
# Image remains inside the disposable private test runtime.
env ALROOT_TEST_IMAGE="$XDG_RUNTIME_DIR/p13-caret.png" GTK_IM_MODULE=fcitx GDK_BACKEND=x11 \
 $base/gtk-caret-probe > "$XDG_RUNTIME_DIR/gtk-caret.log" 2>&1 & gui_pid=$!
sleep .65
send_series
wait "$gui_pid";gui_pid=""
grep -q "GTK_P13_SCREENSHOT_SAVED=1" "$XDG_RUNTIME_DIR/gtk-caret.log"
test -s "$XDG_RUNTIME_DIR/p13-caret.png"
echo "PASS: Native GTK visual caret image captured with no underline format"
# Verify deletion of a committed Korean syllable and the next composing
# update; old XTEST is still used for committed DEL even with GTK Fcitx.
# Also check that normal English-to-Korean entry remains non-duplicated.
for scenario in deletion switch echo echo-p16; do
    expected=글
    [[ "$scenario" == switch ]] && expected=hi하
    [[ "$scenario" == echo || "$scenario" == echo-p16 ]] && expected=hello
    env GTK_IM_MODULE=fcitx GDK_BACKEND=x11 \
        $base/gtk-edit-probe "$expected" > "$XDG_RUNTIME_DIR/gtk-edit-$scenario.log" 2>&1 & gui_pid=$!
    sleep .58
    if [[ "$scenario" == echo || "$scenario" == echo-p16 ]]; then
        probe_extra=()
        [[ "$scenario" != echo-p16 ]] || probe_extra=(p16)
        java -cp "$base/jvm" ImeEnglishEchoNativeProbe "$XDG_RUNTIME_DIR/ime.sock" \
            "${probe_extra[@]}" \
            > "$XDG_RUNTIME_DIR/echo-test.log" 2>&1
        cat "$XDG_RUNTIME_DIR/echo-test.log"
    else
    python3 - "$XDG_RUNTIME_DIR/ime.sock" "$scenario" <<"PY"
import socket,struct,sys,time
s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1])
if sys.argv[2]=="deletion":
    series=[("C","한"),("C","하"),("T","하"),("D","1 0"),("C","글"),("T","글")]
else:
    series=[("C","hi"),("T","hi"),("C","한"),("C","하"),("T","하")]
for op,text in series:
    b=text.encode()
    s.sendall(op.encode()+struct.pack("!I",len(b))+b)
    time.sleep(.085)
s.close()
PY
    fi
    wait "$gui_pid";gui_pid=""
    cat "$XDG_RUNTIME_DIR/gtk-edit-$scenario.log"
    grep -q "P11_NATIVE_EDIT_MATCH=1" "$XDG_RUNTIME_DIR/gtk-edit-$scenario.log"
    if [[ "$scenario" == echo || "$scenario" == echo-p16 ]]; then
        if [[ "$scenario" == echo-p16 ]]; then
            grep -q "P16_PREFIX_REPLAY_FILTERED=1" "$XDG_RUNTIME_DIR/echo-test.log"
        else
            grep -q "P12_ENGLISH_ECHO_FILTERED=1" "$XDG_RUNTIME_DIR/echo-test.log"
            grep -q "P13_PHANTOM_DELETE_IGNORED=2" "$XDG_RUNTIME_DIR/echo-test.log"
        fi
    fi
    echo "PASS: native GTK $scenario edits preserve text and final cursor"
done
# Focus away from native GTK to raw Xlib: it must now fall back again.
$base/xlib-fallback > "$XDG_RUNTIME_DIR/raw-after.log" 2>&1 & gui_pid=$!
sleep .65
send_series
wait "$gui_pid";gui_pid=""
cat "$XDG_RUNTIME_DIR/raw-after.log"
grep -q 'success=1' "$XDG_RUNTIME_DIR/raw-after.log"
echo "PASS: Raw Xlib after focused GTK still uses XTEST fallback"
# Separate XFCE VTE/GTK client receives native preedit and final text.
env GTK_IM_MODULE=fcitx GDK_BACKEND=x11 \
 xfce4-terminal --disable-server --title "P11 private native terminal" \
  --command="$HOME/alroot_wayland/tests/p11-terminal-consumer.sh $XDG_RUNTIME_DIR/terminal-result.txt" \
  > "$XDG_RUNTIME_DIR/terminal.log" 2>&1 & gui_pid=$!
for i in $(seq 1 35); do
  win=$(xdotool search --name "P11 private native terminal" 2>/dev/null | head -1 || true)
  [[ -n "$win" ]] && { xdotool windowfocus "$win" 2>/dev/null || true;break;}
  sleep .10
done
sleep .35
python3 - "$XDG_RUNTIME_DIR/ime.sock" <<"PY"
import socket,struct,sys,time
s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1])
for op,v in [("C","한"),("T","한"),("C","글"),("T","글")]:
  b=v.encode()
  s.sendall(op.encode()+struct.pack("!I",len(b))+b)
  time.sleep(.06)
s.close()
PY
sleep .12
xdotool key Return
for i in $(seq 1 36); do
  [[ -f "$XDG_RUNTIME_DIR/terminal-result.txt" ]] && break
  sleep .12
done
cat "$XDG_RUNTIME_DIR/terminal-result.txt"
grep -q "PASS_NATIVE_FCITX_TERMINAL=1" "$XDG_RUNTIME_DIR/terminal-result.txt"
echo "PASS: XFCE terminal native preedit/commit through Android-compatible socket"
kill -TERM "$native_pid"
wait "$native_pid" 2>/dev/null || true
echo "=== routing metrics ==="
tail -9 "$XDG_RUNTIME_DIR/native.log"
grep -Eq "native_preedit=[1-9][0-9]*" "$XDG_RUNTIME_DIR/native.log"
grep -Eq "xtest_preedit=[1-9][0-9]*" "$XDG_RUNTIME_DIR/native.log"
grep -Eq "native_del=[1-9][0-9]*" "$XDG_RUNTIME_DIR/native.log"
grep -Eq "uncertain=0" "$XDG_RUNTIME_DIR/native.log"
echo "PASS: Focus-aware native IM, native committed DEL, XTEST fallback, zero ambiguous delivery"
'
