#!/data/data/com.termux/files/usr/bin/bash
# Disposable Xvfb, synthetic GTK input only. No Termux-X11 :0 or Alroot :90.
set -euo pipefail
HOME=/data/data/com.termux/files/home
PREFIX=/data/data/com.termux/files/usr
PROJECT="$HOME/alroot_wayland"
cd "$PROJECT"
mkdir -p .source
work="$(mktemp -d "$PROJECT/.source/p10-smoothing.XXXXXXXX")"
chmod 700 "$work"
xvfb_pid=""; ime_pid=""; gtk_pid=""
cleanup() {
    for p in "$gtk_pid" "$ime_pid" "$xvfb_pid"; do
        [[ -n "$p" ]] && kill "$p" 2>/dev/null || true
    done
}
trap cleanup EXIT
display=""
for n in 196 197 198 199 200 201 202; do
    [[ ! -S "$PREFIX/tmp/.X11-unix/X$n" ]] || continue
    display=":$n"
    break
done
[[ -n "$display" ]] || exit 2
auth="$work/Xauthority"
touch "$auth"
chmod 600 "$auth"
cookie="$(openssl rand -hex 16)"
xauth -f "$auth" add "$display" MIT-MAGIC-COOKIE-1 "$cookie"
unset cookie
Xvfb "$display" -screen 0 960x540x24 -auth "$auth" -nolisten tcp \
  > "$work/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep .45
bash termux/build-ime-server.sh > "$work/native-build.log" 2>&1
env DISPLAY="$display" XAUTHORITY="$auth" \
  "$PROJECT/packages/bin/alroot-ime-server" "$work/ime.sock" \
  > "$work/ime.log" 2>&1 &
ime_pid=$!
sleep .25
[[ -S "$work/ime.sock" ]] || { tail -18 "$work/ime.log"; exit 1; }
clang -O1 tests/p10-ime-gtk-visual.c -o "$work/gtk" \
    $(pkg-config --cflags --libs gtk+-3.0)
mkdir -p "$work/jvm"
javac -encoding UTF-8 -d "$work/jvm" \
    app/src/main/java/com/alroot/wayland/ImeCommitState.java \
    app/src/main/java/com/alroot/wayland/ImeCompositionSmoother.java \
    tests/ImeSmoothBridgeProbe.java
for scenario in delete empty-preedit; do
    env DISPLAY="$display" XAUTHORITY="$auth" GDK_BACKEND=x11 \
        "$work/gtk" > "$work/gtk-$scenario.log" 2>&1 &
    gtk_pid=$!
    sleep .60
    java -cp "$work/jvm" ImeSmoothBridgeProbe "$work/ime.sock" "$scenario" \
        > "$work/java-$scenario.log" 2>&1
    wait "$gtk_pid"
    gtk_pid=""
    grep -q 'final_is_ha=1' "$work/gtk-$scenario.log" || {
        cat "$work/gtk-$scenario.log"; exit 1;
    }
    grep -q 'blank_redraws=0' "$work/gtk-$scenario.log" || {
        cat "$work/gtk-$scenario.log"
        echo 'FAIL: GTK rendered a transient empty frame' >&2
        exit 1
    }
    grep -q 'P10_JAVA_EMPTY_C_FRAMES=0' "$work/java-$scenario.log"
    grep -q 'P10_JAVA_SUPPRESSED_EMPTY=1' "$work/java-$scenario.log"
    echo "PASS: $scenario → GTK Unicode replacement without blank redraw"
    grep 'GTK_FINAL' "$work/gtk-$scenario.log"
done
