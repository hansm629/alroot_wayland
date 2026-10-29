#!/data/data/com.termux/files/usr/bin/bash
# Native EGL or Vulkan client against the new generic AHB wl_buffer.
# No global Vulkan/EGL/GLX/driver changes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-ahb-window.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
    -Wno-unused-function -DALROOT_VK_PRODUCER_ONLY -Igraphics/include \
    -Inative/waylandcomp/generated \
    graphics/tests/wayland-ahb-egl-window.c \
    graphics/tests/ahb-vulkan-cross-process.c \
    native/waylandcomp/generated/alroot-gpu-buffer-v1-protocol.c \
    native/waylandcomp/generated/xdg-shell-protocol.c \
    -lwayland-client -ldl -o "$TMP/test-window"
renderer="${1:-egl}"
seconds="${3:-8}"
case "$renderer" in
egl)
    export ALROOT_AHB_RENDERER=egl
    ;;
vulkan)
    export ALROOT_AHB_RENDERER=vulkan
    export ALROOT_TEST_VULKAN_LOADER=libvulkan.so.1
    case "${2:-wrapper}" in
    wrapper) export VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json" ;;
    turnip) export VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" ;;
    *) echo "unknown Vulkan ICD: ${2:-}" >&2; exit 2 ;;
    esac
    ;;
*) echo 'usage: test-wayland-ahb-window.sh egl|vulkan [wrapper|turnip] [seconds]' >&2; exit 2 ;;
esac
"$TMP/test-window" "$seconds"
