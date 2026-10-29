#!/data/data/com.termux/files/usr/bin/bash
# S1.3: Freedreno KGSL GL memory import of Android AHB + native Wayland.
# This is a direct-AHB producer, not regular Mesa EGL Wayland WSI.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
frames="${1:-90}"
[[ "$frames" =~ ^[0-9]+$ ]] && ((frames>=1 && frames<=360)) || exit 2
OUT="$ROOT/.source/gpu-stage2/kgsl"
mkdir -p "$OUT"
echo 'S1.3-A: require physical KGSL -> AHB -> Android EGL pixel identity'
if ! ALROOT_S13_RESOLUTION=480x320 \
     ALROOT_S13_TEXTURE_STRIDE_WIDTH=1 \
     ALROOT_S13_TEXTURE_HEIGHT=270 \
     ALROOT_S13_SAMPLE_HEIGHT=270 \
     bash graphics/tools/test-s13-kgsl-ahb-capabilities.sh \
       > "$OUT/s13-pixel-preflight.log" 2>&1; then
  grep -E '^S13_|Segmentation|error:' "$OUT/s13-pixel-preflight.log" | tail -18 || :
  echo 'S13_KGSL_AHB_PIXEL_PREFLIGHT=FAIL (refuse unsafe image sharing)' >&2
  exit 6
fi
grep -qx 'S13_KGSL_AHB_VENDOR_SAMPLE_POINTS=5/5' "$OUT/s13-pixel-preflight.log"
echo 'S13_KGSL_AHB_PIXEL_PREFLIGHT=PASS (five diagnostic readbacks only)'
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s13-kgsl.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
wayland-scanner client-header native/waylandcomp/protocols/viewporter.xml \
  "$TMP/viewporter-client-protocol.h"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
  -Igraphics/include -Inative/waylandcomp/generated -I"$TMP" \
  graphics/tests/s13-kgsl-ahb-wayland.c graphics/src/kgsl_ahb_fd.c \
  graphics/src/ahb_wayland_presenter.c \
  native/waylandcomp/generated/alroot-gpu-buffer-v1-protocol.c \
  native/waylandcomp/generated/xdg-shell-protocol.c \
  native/waylandcomp/generated/viewporter-protocol.c \
  -lwayland-client -ldl -o "$TMP/s13-kgsl"
timestamp="$(date +'%m-%d %H:%M:%S.000')"
set +e
if [[ "${ALROOT_S13_WIRE_VERIFY:-0}" == 1 ]]; then
  WIRE_LOG="$OUT/wire-s13-kgsl-${frames}.log"
  timeout -s TERM 22s env -u DISPLAY -u VK_DRIVER_FILES \
    MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
    EGL_PLATFORM=surfaceless WAYLAND_DEBUG=client \
    XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
    WAYLAND_DISPLAY=wayland-0 "$TMP/s13-kgsl" "$frames" \
    > "$WIRE_LOG" 2>&1
  rc=$?
  grep '^S13_' "$WIRE_LOG" || :
else
  timeout -s TERM 22s env -u DISPLAY -u VK_DRIVER_FILES \
    MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
    EGL_PLATFORM=surfaceless \
    XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
    WAYLAND_DISPLAY=wayland-0 "$TMP/s13-kgsl" "$frames"
  rc=$?
fi
set -e
HOST_LOG="$OUT/host-s13-kgsl-${frames}.log"
logcat -d -t "$timestamp" -s AlrootWayland >"$HOST_LOG" 2>&1 || :
if ((rc!=0)); then
  echo "S13_KGSL_AHB_TEST=FAILED_${rc}"
  exit "$rc"
fi
if [[ "${ALROOT_S13_WIRE_VERIFY:-0}" == 1 ]]; then
  ahb_count="$(grep -Ec ' -> alroot_gpu_buffer_v1#[0-9]+\.create_buffer(_with_format)?\(' "$WIRE_LOG" || true)"
  shm_count="$(grep -Ec ' -> wl_shm_pool#[0-9]+\.create_buffer\(' "$WIRE_LOG" || true)"
  dma_count="$(grep -Ec ' -> zwp_linux_buffer_params_v1#[0-9]+\.create(_immed)?\(' "$WIRE_LOG" || true)"
  echo "S13_WIRE_AHB_BUFFERS=$ahb_count"
  echo "S13_WIRE_SHM_BUFFERS=$shm_count"
  echo "S13_WIRE_DMA_BUFFERS=$dma_count"
  if ((ahb_count==0 || shm_count!=0 || dma_count!=0)); then
    echo 'S13_KGSL_WIRE_GPU_TRANSPORT=FAIL' >&2
    exit 6
  fi
  echo 'S13_KGSL_WIRE_GPU_TRANSPORT=AHB_ONLY'
fi
if grep -q 'AHB -> VkImage GPU-copy:' "$HOST_LOG" &&
   grep -q '"Alroot S1.3 KGSL AHB presenter".*presenting GPU frames through Wayland' "$HOST_LOG"; then
  echo 'S13_KGSL_AHB_GPU_COPY=HOST_IMPORT_CONFIRMED'
else
  echo 'S13_KGSL_AHB_GPU_COPY=NOT_CONFIRMED'
  grep -Ei 'import failed|gpu-copy|KGSL AHB' "$HOST_LOG" | tail -8 || :
  exit 5
fi
echo 'S13_KGSL_AHB_CPU_PIXEL_COPY=0 (application GPU FBO -> AHB only)'
echo 'VISUAL_ACCEPTANCE=USER_CHECK_REQUIRED'
echo "S13_HOST_LOG=$HOST_LOG"
