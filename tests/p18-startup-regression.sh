#!/data/data/com.termux/files/usr/bin/bash
# Android-free policy tests for first editor connection and initial pointer.
# Real Samsung and Android-frame rendering remain separate user checks.
set -euo pipefail
project=/data/data/com.termux/files/home/alroot_wayland
cd "$project"
dest="$project/.source/p18-jvm"
mkdir -p "$dest"
javac -encoding UTF-8 -d "$dest" \
    app/src/main/java/com/alroot/wayland/ImeConnectionHolder.java \
    app/src/main/java/com/alroot/wayland/ImeCursorTracker.java \
    app/src/main/java/com/alroot/wayland/PointerStartupPolicy.java \
    tests/ImeConnectionHolderTest.java \
    tests/ImeCursorTrackerTest.java \
    tests/PointerStartupPolicyTest.java
java -cp "$dest" com.alroot.wayland.ImeConnectionHolderTest
java -cp "$dest" com.alroot.wayland.PointerStartupPolicyTest
java -cp "$dest" ImeCursorTrackerTest
echo 'PASS: P18 first-connection, phantom selection and centered pointer policies'
echo 'NOTE: Android Samsung keyboard and startup cursor still need real screen acceptance'
