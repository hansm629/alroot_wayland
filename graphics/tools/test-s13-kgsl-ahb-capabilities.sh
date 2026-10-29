#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s13-kgsl.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
  -Igraphics/include graphics/tests/s13-kgsl-ahb-capabilities.c \
  graphics/src/kgsl_ahb_fd.c -lEGL -lGLESv2 -ldl \
  -o "$TMP/probe"
log="$ROOT/.source/gpu-stage2/kgsl/probe-${ALROOT_S13_RESOLUTION:-64x64}.log"
mkdir -p "$(dirname "$log")"
MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
  EGL_PLATFORM=surfaceless timeout 12s "$TMP/probe" | tee "$log"
grep -qx 'S13_KGSL_AHB_GL_FD0_CROSS_DRIVER_PIXELS=PASS' "$log" || {
    echo 'S13_KGSL_AHB_PIXEL_SHARING=NOT_VERIFIED' >&2
    exit 5
}
echo 'S13_KGSL_AHB_PIXEL_SHARING=PASS'
