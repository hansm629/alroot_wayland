#!/data/data/com.termux/files/usr/bin/bash
# S1.5 host-backed Alroot AHB WSI acceptance for pinned Mesa 27 Turnip.
# Success requires: real Turnip GPU memory import with five correct pixels,
# AHB wl_buffer wire transfer (no linux-dmabuf/shm), host VkImage import and
# a correctly exited Vulkan Wayland client. The display image still needs
# user visual inspection. Never auto-install/restart anything.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
FRAMES="${1:-120}"
[[ "$FRAMES" =~ ^[0-9]+$ ]] && ((FRAMES>=1&&FRAMES<=360)) || exit 2
PRESENT_MODE="${ALROOT_S15_PRESENT_MODE:-2}"
case "$PRESENT_MODE" in 0|1|2) ;; *) echo 'S15_PRESENT_MODE=INVALID' >&2;exit 2;; esac
PIN='6f90e92dfdacc4759d8e9ccb328805d0f0b4439b'
BASE="$ROOT/.source/gpu-stage27-pr25"
[[ "$(git -C "$BASE/source" rev-parse HEAD)" == "$PIN" ]] || exit 2
ICD="$BASE/s15-turnip/private-runtime/turnip_alroot_icd.private.json"
LIB="$BASE/s15-turnip/private-runtime/lib/libvulkan_freedreno.so"
[[ -s "$ICD" && -s "$LIB" ]] || {
    echo 'S15_TURNIP_WSI=PRIVATE_ICD_NOT_BUILT' >&2
    exit 2
}
OUT="$BASE/s15-results"
mkdir -p "$OUT"
# Reuse the physical importer probe with the exact same Turnip pinned code.
# It reports external-buffer caps as 0, but actual imported AHB GPU writes
# are validated rather than relying on a driver feature string.
ALROOT_S15_DIAGNOSTIC_TRY_UNADVERTISED_IMPORT=1 \
ALROOT_S15_AHB_RESOLUTION=640x360 \
ALROOT_S15_TEST_ICD="$ICD" \
  bash graphics/tools/test-s15-turnip-ahb-buffer-import.sh \
  > "$OUT/wayland-preflight.log" 2>&1 || {
      echo 'S15_TURNIP_WSI_PHYSICAL_AHB_PIXELS=FAIL' >&2
      tail -20 "$OUT/wayland-preflight.log"
      exit 6
  }
grep -qx 'S15_TURNIP_AHB_BUFFER_GPU_WRITE=PASS' "$OUT/wayland-preflight.log"
echo 'S15_TURNIP_WSI_PHYSICAL_AHB_PIXELS=5_OF_5_PASS'

export VK_DRIVER_FILES="$ICD"
export LD_LIBRARY_PATH="$BASE/s15-turnip/private-runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ALROOT_TURNIP_WAYLAND_AHB=1
export XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
VK_LOG="$OUT/turnip-alroot-vulkaninfo.log"
timeout 10s vulkaninfo --summary > "$VK_LOG" 2>&1
if ! grep -q 'driverInfo.*27.0.0-devel (git-6f90e92' "$VK_LOG"; then
    echo 'S15_TURNIP_WSI_ICD=WRONG_MESA_SOURCE' >&2
    exit 4
fi
echo 'S15_TURNIP_WSI_ICD=MESA27_6F90E92'
LOG="$OUT/turnip-alroot-vkcube-${FRAMES}.log"
HOST="$OUT/turnip-alroot-host-${FRAMES}.log"
timestamp="$(date +'%m-%d %H:%M:%S.000')"
set +e
timeout -s TERM 25s env -u DISPLAY -u VK_ICD_FILENAMES \
    WAYLAND_DEBUG=client \
    vkcube --wsi wayland --c "$FRAMES" --width 640 --height 360 \
      --present_mode "$PRESENT_MODE" >"$LOG" 2>&1
rc=$?
set -e
logcat -d -t "$timestamp" -s AlrootWayland >"$HOST" 2>&1 || :
echo "S15_TURNIP_WSI_VKCUBE_EXIT=$rc"
echo "S15_TURNIP_WSI_PRESENT_MODE=$PRESENT_MODE"
grep -E '^S15_WSI_|Selected GPU|VK_ERROR|ERROR|error|Aborted|Segmentation' "$LOG" | tail -32 || :
ahb="$(grep -Ec ' -> alroot_gpu_buffer_v1#[0-9]+\.create_buffer(_with_format)?\(' "$LOG" || true)"
dma="$(grep -Ec ' -> zwp_linux_buffer_params_v1#[0-9]+\.create(_immed)?\(' "$LOG" || true)"
shm="$(grep -Ec ' -> wl_shm_pool#[0-9]+\.create_buffer\(' "$LOG" || true)"
echo "S15_TURNIP_WSI_AHB_BUFFER_COUNT=$ahb"
echo "S15_TURNIP_WSI_DMABUF_BUFFER_COUNT=$dma"
echo "S15_TURNIP_WSI_SHM_BUFFER_COUNT=$shm"
if ((rc!=0 || ahb<1 || dma!=0 || shm!=0)); then
    echo 'S15_TURNIP_WSI_WIRE=FAIL' >&2
    tail -21 "$LOG"
    exit 5
fi
echo 'S15_TURNIP_WSI_WIRE=AHB_ONLY'
if grep -q 'AHB -> VkImage GPU-copy:' "$HOST" &&
   grep -Ei '"vkcube".*presenting GPU frames through Wayland' "$HOST"; then
    echo 'S15_TURNIP_WSI_GPU_COPY=HOST_IMPORT_CONFIRMED'
else
    echo 'S15_TURNIP_WSI_GPU_COPY=NOT_CONFIRMED'
    grep -Ei 'failed|import|vkcube|[Ee]rror' "$HOST" | tail -15 || :
    exit 5
fi
echo 'S15_TURNIP_WSI_CPU_PIXEL_COPY=0_GPU_COPY_ONLY'
echo 'S15_TURNIP_WSI_ZERO_COPY=NOT_CLAIMED'
echo 'S15_TURNIP_WSI_VISUAL=USER_CHECK_REQUIRED'
echo "S15_TURNIP_WSI_CLIENT_LOG=$LOG"
echo "S15_TURNIP_WSI_HOST_LOG=$HOST"
