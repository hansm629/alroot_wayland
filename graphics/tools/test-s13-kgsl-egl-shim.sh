#!/data/data/com.termux/files/usr/bin/bash
# Experimental S1.3-B EGL Wayland KGSL -> GL GPU blit -> AHB -> host VkImage.
# Opt-in in the single client process. Not a globally installed EGL or DRI3 change.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
frames="${1:-90}"
[[ "$frames" =~ ^[0-9]+$ ]] && ((frames>0&&frames<=360)) || {
  echo 'usage: test-s13-kgsl-egl-shim.sh [1..360 frames]' >&2
  exit 2
}
OUT="$ROOT/.source/gpu-stage2/kgsl/s13-egl-shim-results"
mkdir -p "$OUT"
# Prior 480x270 AHB images appeared valid but did NOT contain correct pixels.
# Only opt in after checking every sample in the padded 480x320 layout.
if ! ALROOT_S13_RESOLUTION=480x320 \
     ALROOT_S13_TEXTURE_STRIDE_WIDTH=1 \
     ALROOT_S13_TEXTURE_HEIGHT=270 \
     ALROOT_S13_SAMPLE_HEIGHT=270 \
     bash graphics/tools/test-s13-kgsl-ahb-capabilities.sh \
      >"$OUT/pixel-preflight.log" 2>&1; then
  tail -18 "$OUT/pixel-preflight.log"
  echo 'S13_EGL_SHIM_PIXEL_PREFLIGHT=FAIL' >&2
  exit 5
fi
grep -qx 'S13_KGSL_AHB_VENDOR_SAMPLE_POINTS=5/5' "$OUT/pixel-preflight.log"
echo 'S13_EGL_SHIM_PIXEL_PREFLIGHT=PASS'

bash graphics/tools/build-s13-kgsl-egl-shim.sh >"$OUT/shim-build.log" 2>&1
SHIM="$ROOT/.source/gpu-stage2/kgsl/egl-shim/libalroot_kgsl_egl_ahb.so"
[[ -s "$SHIM" ]] || exit 2
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s13-egl.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
clang --target=aarch64-linux-android35 -std=c11 -Wall -Wextra -Werror \
  -Inative/waylandcomp/generated graphics/tests/wayland-egl-swaps.c \
  native/waylandcomp/generated/xdg-shell-protocol.c \
  -lwayland-client -lwayland-egl -lEGL -lGLESv2 \
  -o "$TMP/wayland-egl"

stamp="$(date +'%m-%d %H:%M:%S.000')"
LOG="$OUT/client-${frames}.log"
set +e
timeout -s TERM 24s env -u DISPLAY -u VK_DRIVER_FILES \
  -u LIBGL_ALWAYS_SOFTWARE -u __EGL_VENDOR_LIBRARY_FILENAMES \
  LD_PRELOAD="$SHIM" ALROOT_S13_KGSL_EGL_AHB=1 \
  WAYLAND_DEBUG=client MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
  EGL_PLATFORM=wayland \
  XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" WAYLAND_DISPLAY=wayland-0 \
  "$TMP/wayland-egl" "$frames" >"$LOG" 2>&1
rc=$?
set -e
HOST_LOG="$OUT/host-${frames}.log"
logcat -d -t "$stamp" -s AlrootWayland >"$HOST_LOG" 2>&1 || :
grep -E '^S12_|^S13_EGL_SHIM' "$LOG" || :
if ((rc!=0)); then
    echo "S13_KGSL_EGL_SHIM_EXIT=$rc"
    tail -16 "$LOG"
    exit "$rc"
fi
# Host and client must agree on buffer transport and complete release events.
ahb="$(grep -Ec ' -> alroot_gpu_buffer_v1#[0-9]+\.create_buffer(_with_format)?\(' "$LOG" || true)"
shm="$(grep -Ec ' -> wl_shm_pool#[0-9]+\.create_buffer\(' "$LOG" || true)"
dma="$(grep -Ec ' -> zwp_linux_buffer_params_v1#[0-9]+\.create(_immed)?\(' "$LOG" || true)"
echo "S13_KGSL_EGL_WIRE_AHB_BUFFERS=$ahb"
echo "S13_KGSL_EGL_WIRE_SHM_BUFFERS=$shm"
echo "S13_KGSL_EGL_WIRE_DMA_BUFFERS=$dma"
if ((ahb==0||shm!=0||dma!=0)) ||
   ! grep -qx "S12_EGL_SWAP_SUCCESS_COUNT=$frames" "$LOG" ||
   ! grep -Eq "S13_EGL_SHIM_STATS submitted=$frames released=$frames" "$LOG" ||
   ! grep -q 'AHB -> VkImage GPU-copy:' "$HOST_LOG" ||
   ! grep -q '"Alroot S1.2 Wayland EGL/GLES swap test".*presenting GPU frames through Wayland' "$HOST_LOG"; then
    echo 'S13_KGSL_EGL_SHIM_TRANSPORT=FAIL' >&2
    exit 6
fi
if grep -E 'AHB buffer accounting underflow|Vulkan device lost' "$HOST_LOG"; then
    echo 'S13_KGSL_EGL_SHIM_HOST_STABILITY=FAIL' >&2
    exit 7
fi
echo 'S13_KGSL_EGL_SHIM_GPU_COPY=HOST_IMPORT_CONFIRMED'
echo 'S13_KGSL_EGL_SHIM_FRAMES_AND_RELEASE=PASS'
echo 'S13_KGSL_EGL_SHIM_CPU_PIXEL_COPY=0 (GL GPU-blit + VkImage host copy)'
echo 'S13_KGSL_EGL_SHIM_STANDARD_COMPATIBILITY=EXPERIMENTAL'
echo 'VISUAL_ACCEPTANCE=USER_CHECK_REQUIRED'
echo "S13_KGSL_EGL_SHIM_LOGS=$OUT"
