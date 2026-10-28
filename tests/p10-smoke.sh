#!/data/data/com.termux/files/usr/bin/bash
# P10: no original Termux-X11 :0 or active Alroot :90 keyboard injection.
# Re-run P09 responsiveness plus P10 display-frame coalescing tests.
set -euo pipefail
PROJECT=/data/data/com.termux/files/home/alroot_wayland
cd "$PROJECT"
mkdir -p .source/p10-jvm
javac -encoding UTF-8 -d .source/p10-jvm \
    app/src/main/java/com/alroot/wayland/ImeCommitState.java \
    app/src/main/java/com/alroot/wayland/ImeCompositionSmoother.java \
    app/src/main/java/com/alroot/wayland/ImeBackspaceGuard.java \
    tests/ImeCommitStateTest.java \
    tests/ImeCompositionSmootherTest.java \
    tests/ImeBackspaceGuardTest.java
java -cp .source/p10-jvm ImeCommitStateTest
java -cp .source/p10-jvm ImeCompositionSmootherTest
java -cp .source/p10-jvm ImeBackspaceGuardTest
bash tests/p09-ime-realtime.sh
bash tests/p10-ime-smoothing.sh
bash tests/p04-ime-headless.sh
bash tests/p06-ime-distinct.sh
echo 'PASS: P10 Korean deletion smoothing, no blank GTK redraw, P09 immediate entry preserved'
