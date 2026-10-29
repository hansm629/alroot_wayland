#!/data/data/com.termux/files/usr/bin/bash
# Queries a separate, unbuffered xdg_toplevel against current Wayland.
# No APK/desktop/X server restart, no keyboard input or GPU allocation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s11-xdg.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=c11 -Wall -Wextra -Werror \
    -Inative/waylandcomp/generated \
    graphics/tests/test-s11-xdg-protocol.c \
    native/waylandcomp/generated/xdg-shell-protocol.c \
    -lwayland-client -o "$TMP/probe"
env -u DISPLAY XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
    WAYLAND_DISPLAY=wayland-0 "$TMP/probe"
