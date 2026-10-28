#!/data/data/com.termux/files/usr/bin/bash
# P13: synthesize input only to private Xvfb; never active :0 or :90.
set -euo pipefail
PROJECT=/data/data/com.termux/files/home/alroot_wayland
cd "$PROJECT"
mkdir -p .source/p13-jvm
javac -encoding UTF-8 -d .source/p13-jvm \
    app/src/main/java/com/alroot/wayland/ImeCursorTracker.java \
    tests/ImeCursorTrackerTest.java
java -cp .source/p13-jvm ImeCursorTrackerTest
bash tests/p12-smoke.sh
bash tests/p10-ime-smoothing.sh
echo 'PASS: P13 native cursor, native GTK DEL, phantom IME cleanup, fallback'
echo 'NOTE: real Samsung keyboard display must still be tested on device'
