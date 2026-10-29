#!/data/data/com.termux/files/usr/bin/bash
# Offline test: exactly the XDG size policy compiled into Alroot's host.
# Never restarts the live P20 XFCE / :90 or Termux-X11 :0 sessions.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s11-geometry.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
clang -std=c11 -Wall -Wextra -Werror \
    -Inative/waylandcomp/src graphics/tests/test-s11-geometry.c \
    -o "$TMP/geometry"
"$TMP/geometry"
