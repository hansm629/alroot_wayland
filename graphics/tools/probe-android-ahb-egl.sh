#!/data/data/com.termux/files/usr/bin/bash
# Read-only test in a separate process; never runs on the active Wayland EGL.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
tmp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-android-ahb-egl.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
    graphics/tools/probe-android-ahb-egl.c -ldl -o "$tmp/probe"
"$tmp/probe"
