#!/data/data/com.termux/files/usr/bin/bash
# Project-only Wayland protocol client, no package download/global installs.
set -euo pipefail
HOME=/data/data/com.termux/files/home
PREFIX=/data/data/com.termux/files/usr
PROJECT="$HOME/alroot_wayland"
SRC="$PROJECT/termux"
BIN="$PROJECT/packages/bin/alroot-nested-output"
GEN="$PROJECT/.source/nested-output-build"
mkdir -p "$GEN" "$(dirname "$BIN")"
PROTO="$SRC/protocols/wlr-output-management-unstable-v1.xml"
if [[ ! -x "$BIN" || "$SRC/alroot-nested-output.c" -nt "$BIN" ||
      "$PROTO" -nt "$BIN" ]]; then
    wayland-scanner client-header "$PROTO" "$GEN/wlr-output-management-client.h"
    wayland-scanner private-code "$PROTO" "$GEN/wlr-output-management-client.c"
    clang -O2 -Wno-unused-parameter -I"$GEN" \
          "$SRC/alroot-nested-output.c" \
          "$GEN/wlr-output-management-client.c" \
          -o "$GEN/alroot-nested-output" \
          $(pkg-config --cflags --libs wayland-client)
    chmod 700 "$GEN/alroot-nested-output"
    mv -f "$GEN/alroot-nested-output" "$BIN"
fi
echo "PASS: private nested Wayland output resizer built"
