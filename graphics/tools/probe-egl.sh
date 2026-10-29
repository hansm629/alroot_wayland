#!/data/data/com.termux/files/usr/bin/bash
# Execute offscreen EGL queries on Termux's EXISTING libraries; never change
# default EGL_PLATFORM or driver overrides in the current shell/session.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
bin_dir="$(mktemp -d "${TMPDIR:-/data/data/com.termux/files/usr/tmp}/alroot-egl-probe.XXXXXXXX")"
trap 'rm -rf "$bin_dir"' EXIT
clang -std=c11 -Wall -Wextra -I"${PREFIX:-/data/data/com.termux/files/usr}/include" \
  graphics/tools/egl-capability-probe.c -o "$bin_dir/probe" \
  -lEGL -lGLESv2 -lwayland-client
"$bin_dir/probe" "${1:-surfaceless}"
