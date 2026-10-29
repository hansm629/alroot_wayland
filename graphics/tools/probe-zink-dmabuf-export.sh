#!/data/data/com.termux/files/usr/bin/bash
# Tests the REAL texture export using scoped Mesa Zink + Vulkan-wrapper ICD.
# Does not install/replace EGL, Vulkan or any DRI3 server.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-zink-export.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
    -Igraphics/include graphics/tests/egl-export-dmabuf.c \
    graphics/tests/vulkan-import-egl-fd.c -lEGL -lGLESv2 -ldl -o "$TMP/probe"
export MESA_LOADER_DRIVER_OVERRIDE=zink
export GALLIUM_DRIVER=zink
export VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json"
export ALROOT_TEST_VENDOR_FD_IMPORT=1
"$TMP/probe"
