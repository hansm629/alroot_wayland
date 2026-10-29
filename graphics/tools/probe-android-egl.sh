#!/data/data/com.termux/files/usr/bin/bash
# Probe the Android system loader in a one-off process. Not Termux Mesa EGL.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
temp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-android-egl.XXXXXXXX")"
trap 'rm -rf "$temp"' EXIT
cd "$ROOT"
clang -std=gnu11 -Wall -Wextra -Werror \
    graphics/tools/probe-android-egl.c -ldl -o "$temp/probe"
"$temp/probe"
