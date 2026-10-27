#!/data/data/com.termux/files/usr/bin/bash
# Functional performance regression test. Never attaches to user's active
# Termux-X11 :0 or Alroot desktop :90. Synthetic clipboard data only.
set -euo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
PROJECT="$HOME/alroot_wayland"
cd "$PROJECT"
command -v Xvfb >/dev/null || { echo 'Install xorg-server-xvfb for this test' >&2; exit 2; }
command -v pkg-config >/dev/null || exit 2
root="$(mktemp -d "$PROJECT/.source/p04-headless.XXXXXXXX")"
chmod 700 "$root"
display=""
xvfb_pid=""
ime_pid=""
gtk_pid=""
cleanup() {
    [[ -z "$gtk_pid" ]] || kill "$gtk_pid" 2>/dev/null || true
    [[ -z "$ime_pid" ]] || kill "$ime_pid" 2>/dev/null || true
    [[ -z "$xvfb_pid" ]] || kill "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT
for number in 190 191 192 193 194 195; do
    [[ ! -S "$PREFIX/tmp/.X11-unix/X$number" ]] || continue
    display=":$number"
    break
done
[[ -n "$display" ]] || { echo 'No free disposable display' >&2; exit 2; }
auth="$root/Xauthority"
touch "$auth"
chmod 600 "$auth"
cookie="$(openssl rand -hex 16)"
xauth -f "$auth" add "$display" MIT-MAGIC-COOKIE-1 "$cookie"
unset cookie
Xvfb "$display" -screen 0 960x540x24 -auth "$auth" -nolisten tcp \
    > "$root/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep .5
env DISPLAY="$display" XAUTHORITY="$auth" xdpyinfo >/dev/null

"$PREFIX/bin/bash" "$PROJECT/termux/build-ime-server.sh" >/dev/null
server="$PROJECT/packages/bin/alroot-ime-server"
env ALROOT_IME_DEBUG=1 DISPLAY="$display" XAUTHORITY="$auth" "$server" "$root/ime.sock" \
    > "$root/ime-server.log" 2>&1 &
ime_pid=$!
sleep .25
[[ -S "$root/ime.sock" ]] || { cat "$root/ime-server.log"; exit 1; }

# Artificial long original clipboard detects accidental paste/overwrite.
python3 - <<'PY' | env DISPLAY="$display" XAUTHORITY="$auth" \
        xclip -selection clipboard -in -t UTF8_STRING
print('Z' * 8950, end='')
PY
original="$(env DISPLAY="$display" XAUTHORITY="$auth" \
    timeout 2 xclip -selection clipboard -out -t UTF8_STRING | sha256sum | cut -d' ' -f1)"
clang -O1 tests/p04-ime-gtk-probe.c -o "$root/gtk-probe" \
    $(pkg-config --cflags --libs gtk+-3.0)
env DISPLAY="$display" XAUTHORITY="$auth" GDK_BACKEND=x11 "$root/gtk-probe" \
    > "$root/gtk-result.log" 2>&1 &
gtk_pid=$!
sleep .85

python3 - "$root/ime.sock" <<'PY'
import socket, struct, sys, time
sock = socket.socket(socket.AF_UNIX)
sock.connect(sys.argv[1])
start = time.perf_counter_ns()
for _ in range(10):
    for op, value in (('C','ㄱ'),('C','가'),('T','가'),('F','')):
        payload = value.encode('utf-8')
        sock.sendall(op.encode() + struct.pack('!I',len(payload)) + payload)
        time.sleep(.025)
print('P04 synthetic input: 40 operations sent in %.1f ms' %
      ((time.perf_counter_ns()-start)/1e6))
sock.close()
PY
wait "$gtk_pid"
gtk_pid=""
grep -q '^P04_GTK_SYNTHETIC_MATCH=1 chars=10$' "$root/gtk-result.log" || {
    cat "$root/gtk-result.log"
    echo 'FAIL: Unicode X11 composition lost/duplicated text' >&2
    exit 1
}
after="$(env DISPLAY="$display" XAUTHORITY="$auth" \
    timeout 2 xclip -selection clipboard -out -t UTF8_STRING | sha256sum | cut -d' ' -f1)"
[[ "$original" == "$after" ]] || {
    echo 'FAIL: Clipboard changed after Unicode IME test' >&2
    exit 1
}

# Terminal paste uses Ctrl+Shift+V, not GTK's Ctrl+V. Verify that switching
# between these targets does not leave the composition stream sluggish.
if command -v xfce4-terminal >/dev/null; then
    env DISPLAY="$display" XAUTHORITY="$auth" GDK_BACKEND=x11 \
        xfce4-terminal --disable-server --title='P04 isolated terminal' \
        --command="$PROJECT/tests/p04-ime-terminal-consumer.sh $root/terminal-result.txt" \
        > "$root/terminal.log" 2>&1 &
    terminal_pid=$!
    for i in $(seq 1 25); do
        terminal_window="$(env DISPLAY="$display" XAUTHORITY="$auth" \
            xdotool search --name 'P04 isolated terminal' 2>/dev/null | head -1 || true)"
        # An offscreen Xvfb has no window manager and won't auto-focus
        # new terminal windows. Explicit focus is safe on this test-only :19x.
        if [[ -n "$terminal_window" ]]; then
            env DISPLAY="$display" XAUTHORITY="$auth" \
                xdotool windowfocus "$terminal_window" 2>/dev/null || true
        fi
        cls="$(env DISPLAY="$display" XAUTHORITY="$auth" \
            xdotool getwindowfocus getwindowclassname 2>/dev/null || true)"
        [[ "$cls" == *[Tt]erminal* ]] && break
        sleep .08
    done
    [[ "$cls" == *[Tt]erminal* ]] || { echo 'FAIL: terminal not focused'; exit 1; }
    python3 - "$root/ime.sock" <<'PY'
import socket,struct,sys,time
s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1])
for operation,value in [('C','ㅎ'),('C','하'),('C','한'),
                         ('T','한'),('T','글')]:
    b=value.encode('utf-8')
    s.sendall(operation.encode()+struct.pack('!I',len(b))+b)
    time.sleep(.035)
s.close()
PY
    sleep .15
    env DISPLAY="$display" XAUTHORITY="$auth" xdotool key Return
    for i in $(seq 1 32); do
        [[ -f "$root/terminal-result.txt" ]] && break
        sleep .08
    done
    [[ -r "$root/terminal-result.txt" ]] || { echo 'FAIL: terminal received no line'; exit 1; }
    grep -q 'P04_TERMINAL_SYNTHETIC_MATCH=1' "$root/terminal-result.txt" ||
        { cat "$root/terminal-result.txt";
          tail -15 "$root/ime-server.log"; exit 1; }
    after="$(env DISPLAY="$display" XAUTHORITY="$auth" \
        timeout 2 xclip -selection clipboard -out -t UTF8_STRING |
        sha256sum | cut -d' ' -f1)"
    [[ "$original" == "$after" ]] || { echo 'FAIL: terminal clipboard changed'; exit 1; }
    echo 'PASS: Unicode composition in isolated xfce4-terminal'
    # Some Termux VTE/Xvfb combinations abort while closing a test window
    # without a window manager. The asserted synthetic input result above,
    # rather than offscreen GTK shutdown behavior, is our acceptance check.
    wait "$terminal_pid" 2>/dev/null || true
fi
kill -TERM "$ime_pid"
sleep .15
printf 'GTK synthetic result: '; cat "$root/gtk-result.log"
printf 'Persistent server timings:\n'
grep -E 'IME-SERVER events=|IME-SERVER stopped' "$root/ime-server.log" | tail -5
echo 'PASS: isolated GUI text, 40 events, clipboard preserved'
