#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
PROJECT=/data/data/com.termux/files/home/alroot_wayland
STATE=/data/data/com.termux/files/home/.alroot-wayland
PREFIX=/data/data/com.termux/files/usr
binary="$PROJECT/packages/bin/alroot-battery-tray"
source="$PROJECT/termux/alroot-battery-tray.c"
mkdir -p "$PROJECT/packages/bin"
if [[ ! -x "$binary" || "$source" -nt "$binary" ]]; then
    target="$PROJECT/packages/bin/.alroot-battery-tray.$$"
    trap 'rm -f "$target"' EXIT
    "$PREFIX/bin/clang" -O2 -Wno-deprecated-declarations "$source" \
        -o "$target" $(pkg-config --cflags --libs gtk+-3.0)
    chmod 700 "$target"
    mv "$target" "$binary"
fi
export GDK_BACKEND=x11
exec "$binary" "$STATE/android-battery.json"
