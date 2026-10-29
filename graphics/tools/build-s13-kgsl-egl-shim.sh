#!/data/data/com.termux/files/usr/bin/bash
# Experimental, process-scoped GLES3 KGSL EGL Wayland presenter shim.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage2/kgsl/egl-shim"
mkdir -p "$OUT"
wayland-scanner client-header native/waylandcomp/protocols/viewporter.xml \
  "$OUT/viewporter-client-protocol.h"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
  -shared -fPIC -Igraphics/include -Inative/waylandcomp/generated \
  -I"$OUT" \
  graphics/src/kgsl_egl_ahb_shim.c graphics/src/kgsl_ahb_fd.c \
  graphics/src/ahb_wayland_presenter.c \
  native/waylandcomp/generated/alroot-gpu-buffer-v1-protocol.c \
  native/waylandcomp/generated/viewporter-protocol.c \
  -lEGL -lGLESv2 -lwayland-client -lwayland-egl -ldl \
  -o "$OUT/libalroot_kgsl_egl_ahb.so"
echo "S13_EGL_SHIM_BUILT=$OUT/libalroot_kgsl_egl_ahb.so"
