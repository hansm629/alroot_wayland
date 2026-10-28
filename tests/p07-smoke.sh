#!/data/data/com.termux/files/usr/bin/bash
# P07 preflight; safe and project-only. No keyboard injection into :0/:90.
set -euo pipefail
HOME=/data/data/com.termux/files/home
PROJECT="$HOME/alroot_wayland"
cd "$PROJECT"
mkdir -p .source/p07-java-test
javac -encoding UTF-8 -d .source/p07-java-test \
    app/src/main/java/com/alroot/wayland/ImeCommitState.java \
    app/src/main/java/com/alroot/wayland/TouchpadScrollState.java \
    tests/ImeCommitStateTest.java tests/TouchpadScrollStateTest.java
java -cp .source/p07-java-test ImeCommitStateTest
java -cp .source/p07-java-test TouchpadScrollStateTest
bash -n termux/alroot-wayland termux/alroot-xfce-session.sh
python3 -m py_compile termux/alroot-upower.py
bash tests/p06-smoke.sh
python3 tests/p08-profile-battery.py
bash tests/p08-battery-source.sh
if awstart --status | grep -q '^xfce: RUNNING '; then
    bash tests/p07-upower-live.sh
fi
echo "PASS: P07 synthetic IME/scroll, isolated XFCE battery and PRoot state"
