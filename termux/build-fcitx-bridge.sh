#!/data/data/com.termux/files/usr/bin/bash
# Compile/install the project-private Fcitx5 preedit addon.
# Never changes global Termux fcitx5 libraries, config or user settings.
set -euo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
PROJECT="$HOME/alroot_wayland"
STATE="$HOME/.alroot-wayland"
SOURCE="$PROJECT/termux/alroot-fcitx-bridge.cpp"
LIBRARY="$PROJECT/packages/bin/libalrootbridge.so"
TARGET="$STATE/fcitx-addon/libalrootbridge.so"
METADATA="$STATE/fcitx-data/addon/alrootbridge.conf"

command -v fcitx5 >/dev/null
command -v clang++ >/dev/null
pkg-config --exists Fcitx5Core Fcitx5Utils Fcitx5Config
mkdir -p "$PROJECT/packages/bin" "$STATE/fcitx-addon" \
         "$STATE/fcitx-data/addon"
chmod 700 "$STATE/fcitx-addon" "$STATE/fcitx-data" \
          "$STATE/fcitx-data/addon"
if [[ ! -s "$LIBRARY" || "$SOURCE" -nt "$LIBRARY" ]]; then
    temporary="$PROJECT/packages/bin/.libalrootbridge.$$.so"
    trap 'rm -f "$temporary"' EXIT
    clang++ -std=c++20 -fPIC -shared -O2 -Wall -Wextra \
        -Wno-unused-parameter "$SOURCE" -o "$temporary" \
        $(pkg-config --cflags --libs Fcitx5Core Fcitx5Utils Fcitx5Config)
    chmod 700 "$temporary"
    mv -f "$temporary" "$LIBRARY"
fi
if [[ ! -s "$TARGET" ]] || ! cmp -s "$LIBRARY" "$TARGET"; then
    cp -f "$LIBRARY" "$TARGET"
    chmod 700 "$TARGET"
fi
version="$(pkg-config --modversion Fcitx5Core)"
sed -e "s/^Version=.*/Version=$version/" \
    -e "s/^0=core:.*/0=core:$version/" \
    "$PROJECT/termux/alroot-fcitx-addon.conf" > "$METADATA"
chmod 600 "$METADATA"
echo "PASS: project-only Fcitx5 native IM bridge build version=$version"
