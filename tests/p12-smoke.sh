#!/data/data/com.termux/files/usr/bin/bash
# Focused P12 test: Android stale-English-after-Hangul-DEL and 96 DPI/1080p.
# No synthetic keyboard events are injected into active user :0 or :90.
set -euo pipefail
HOME=/data/data/com.termux/files/home
PROJECT="$HOME/alroot_wayland"
cd "$PROJECT"
mkdir -p .source/p12-jvm
echo 'P12 Android IME state and duplicate Backspace detection'
javac -encoding UTF-8 -d .source/p12-jvm \
    app/src/main/java/com/alroot/wayland/ImeCommitState.java \
    app/src/main/java/com/alroot/wayland/ImeBackspaceGuard.java \
    tests/ImeCommitStateTest.java tests/ImeBackspaceGuardTest.java
java -cp .source/p12-jvm ImeCommitStateTest
java -cp .source/p12-jvm ImeBackspaceGuardTest

echo 'P12 native GTK/Fcitx English -> Hangul -> DEL -> stale English replay'
bash tests/p11-native-integration.sh

echo 'P12 output helper is project-local and preserves X11 96 DPI'
bash termux/build-nested-output.sh
if [[ -S "$HOME/.alroot-wayland/runtime/wayland-1" ]]; then
    test "$(cat "$HOME/.alroot-wayland/labwc-geometry")" = '1920 1080'
    native_dpi="$(DISPLAY=:0 xdpyinfo | sed -n 's/.*resolution: *\([0-9]*\)x.*/\1/p' | head -1)"
    project_dpi="$(DISPLAY=:90 XAUTHORITY="$HOME/.alroot-wayland/Xauthority" \
        xdpyinfo | sed -n 's/.*resolution: *\([0-9]*\)x.*/\1/p' | head -1)"
    [[ "$native_dpi" == 96 && "$project_dpi" == 96 ]]
    echo 'PASS: same 96 DPI on original X11 :0 and Alroot :90; private Labwc 1920x1080'
fi
bash tests/p08-battery-source.sh
python3 tests/p08-profile-battery.py
echo 'PASS: P12 DPI/output and IME regressions; actual Samsung keyboard UX remains user-verified'
