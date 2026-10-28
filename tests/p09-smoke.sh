#!/data/data/com.termux/files/usr/bin/bash
# P09 native/JVM tests are fully isolated from the active :0/:90 session.
set -euo pipefail
ROOT=/data/data/com.termux/files/home/alroot_wayland
cd "$ROOT"
bash tests/p07-smoke.sh
bash tests/p09-ime-realtime.sh
bash tests/p04-ime-headless.sh
echo 'PASS: P09 fast Hangul preedit, responsive Backspace, correct English switch'
echo 'NOTE: final Samsung/One UI keyboard behavior still needs new APK testing.'
