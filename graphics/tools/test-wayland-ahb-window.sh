#!/data/data/com.termux/files/usr/bin/bash
# Client-side demo only; run on a HOST THAT ADVERTISES alroot_gpu_buffer_v1.
# No global Vulkan/EGL/GLX/driver changes. Uses real Android EGL/GLES.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-ahb-window.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
    -Inative/waylandcomp/generated graphics/tests/wayland-ahb-egl-window.c \
    native/waylandcomp/generated/alroot-gpu-buffer-v1-protocol.c \
    native/waylandcomp/generated/xdg-shell-protocol.c \
    -lwayland-client -ldl -o "$TMP/test-window"
"$TMP/test-window" "${1:-8}"
