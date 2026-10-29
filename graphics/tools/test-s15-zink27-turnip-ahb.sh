#!/data/data/com.termux/files/usr/bin/bash
# Mesa27 Zink/Kopper with the *new* private Turnip27 AHB GPU-copy WSI.
# Not the proprietary Vulkan wrapper. No system library modifications.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
FRAMES="${1:-90}"
[[ "$FRAMES" =~ ^[0-9]+$ ]] && ((FRAMES>=1&&FRAMES<=360)) || exit 2
BASE="$ROOT/.source/gpu-stage27-pr25"
[[ "$(git -C "$BASE/source" rev-parse HEAD)" == \
 '6f90e92dfdacc4759d8e9ccb328805d0f0b4439b' ]] || exit 2
GL="$BASE/mesa-private/private-runtime"
ICD="$BASE/s15-turnip/private-runtime/turnip_alroot_icd.private.json"
[[ -s "$GL/lib/libEGL_mesa.so" && -s "$GL/lib/libgallium-27.0.0-devel.so" &&
   -s "$ICD" ]] || { echo 'S15_ZINK_TURNIP_PRIVATE_LIBS=NOT_BUILT' >&2; exit 2; }
OUT="$BASE/s15-results"
mkdir -p "$OUT"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s15-zink.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
clang --target=aarch64-linux-android35 -std=c11 -Wall -Wextra -Werror \
 -Inative/waylandcomp/generated \
 graphics/tests/wayland-egl-swaps.c \
 native/waylandcomp/generated/xdg-shell-protocol.c \
 -lwayland-client -lwayland-egl -lEGL -lGLESv2 -o "$TMP/s15-zink"
export VK_DRIVER_FILES="$ICD"
export ALROOT_TURNIP_WAYLAND_AHB=1
export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink
export ALROOT_EGL_WAYLAND_KOPPER=1
export ALROOT_S12_PRIVATE_EGL=1
export ALROOT_PRIVATE_EGL_MAP_NEEDLE='/gpu-stage27-pr25/mesa-private/private-runtime/lib/libEGL_mesa.so'
export __EGL_VENDOR_LIBRARY_FILENAMES="$GL/egl_vendor.private.json"
export LIBGL_DRIVERS_PATH="$GL/lib/dri"
export LD_LIBRARY_PATH="$GL/lib:$BASE/s15-turnip/private-runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
echo 'S15_ZINK27_BGRA8_AHB_PIXELS=CHECKING'
if ! ALROOT_S15_AHB_FORMAT=bgra8 \
     ALROOT_S15_AHB_RESOLUTION=480x270 \
     ALROOT_S15_TEST_ICD="$ICD" \
     ALROOT_S15_DIAGNOSTIC_TRY_UNADVERTISED_IMPORT=1 \
     bash graphics/tools/test-s15-turnip-ahb-buffer-import.sh \
      >"$OUT/s15-zink-bgra8-pixel-preflight.log" 2>&1; then
  echo 'S15_ZINK27_BGRA8_AHB_PIXELS=NOT_CONFIRMED' >&2
  tail -14 "$OUT/s15-zink-bgra8-pixel-preflight.log" >&2
  exit 6
fi
grep -qx 'S15_TURNIP_AHB_VENDOR_SAMPLE_POINTS=5/5' \
   "$OUT/s15-zink-bgra8-pixel-preflight.log" || exit 6
echo 'S15_ZINK27_BGRA8_AHB_PIXELS=PASS_FIVE_SAMPLES'
stamp="$(date +'%m-%d %H:%M:%S.000')"
LOG="$OUT/s15-zink27-turnip27-${FRAMES}.log"
HOST="$OUT/s15-zink27-turnip27-host-${FRAMES}.log"
set +e
timeout -s TERM 24s env -u DISPLAY -u VK_ICD_FILENAMES WAYLAND_DEBUG=client EGL_PLATFORM=wayland \
 "$TMP/s15-zink" "$FRAMES" >"$LOG" 2>&1
rc=$?
set -e
logcat -d -t "$stamp" -s AlrootWayland > "$HOST" 2>&1 || :
echo 'S15_ZINK27_GPU_BACKEND=MESA27_PINNED_TURNIP_AHB_WSI'
echo "S15_ZINK27_EGL_PROCESS_EXIT=$rc"
grep -E '^S12_|^S15_WSI_|GL_ERROR|VK_ERROR|[Ss]egmentation' "$LOG" | tail -28 || :
if ((rc!=0)) ||
   ! grep -qx 'S12_PRIVATE_EGL_MAPPED=1' "$LOG" ||
   ! grep -q '^S12_GLES_VERSION=.*Mesa 27.0.0-devel (git-6f90e92' "$LOG" ||
   ! grep -q '^S12_GLES_RENDERER=zink Vulkan .*MESA_TURNIP' "$LOG" ||
   ! grep -qx "S12_EGL_SWAP_SUCCESS_COUNT=$FRAMES" "$LOG";then
 echo 'S15_ZINK27_RENDERER_OR_EGL=FAIL' >&2
 exit 5
fi
echo 'S15_ZINK27_EGL_SWAPS=PASS'
ahb="$(grep -Ec ' -> alroot_gpu_buffer_v1#[0-9]+\.create_buffer(_with_format)?\(' "$LOG" || true)"
dma="$(grep -Ec ' -> zwp_linux_buffer_params_v1#[0-9]+\.create(_immed)?\(' "$LOG" || true)"
shm="$(grep -Ec ' -> wl_shm_pool#[0-9]+\.create_buffer\(' "$LOG" || true)"
echo "S15_ZINK27_WIRE_AHB=$ahb"
echo "S15_ZINK27_WIRE_DMABUF=$dma"
echo "S15_ZINK27_WIRE_SHM=$shm"
if ((ahb<1||dma!=0||shm!=0));then
 echo 'S15_ZINK27_WIRE=NOT_AHB_ONLY' >&2
 exit 5
fi
if grep -q 'AHB -> VkImage GPU-copy:' "$HOST" &&
   grep -q '"Alroot S1.2 Wayland EGL/GLES swap test".*presenting GPU frames through Wayland' "$HOST";then
 echo 'S15_ZINK27_GPU_COPY=HOST_IMPORT_CONFIRMED'
else
 echo 'S15_ZINK27_GPU_COPY=NOT_CONFIRMED'
 grep -Ei 'import|fail|error' "$HOST" | tail -12 || :
 exit 5
fi
echo 'S15_ZINK27_VISUAL=USER_CHECK_REQUIRED'
echo "S15_ZINK27_LOG=$LOG"
echo "S15_ZINK27_HOST_LOG=$HOST"
