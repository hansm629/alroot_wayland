#!/data/data/com.termux/files/usr/bin/bash
# Pinned lfdevs PR #25 Mesa 27 Wayland EGL client verification, no installation.
# Separately identify the native GL driver and the Vulkan ICD.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
mode="${1:-zink}"
frames="${2:-60}"
icd="${3:-wrapper}"
[[ "$frames" =~ ^[0-9]+$ ]] && ((frames>0 && frames<=360)) || exit 2
case "$mode:$icd" in
 zink:wrapper|zink:turnip|kgsl:none|kgsl:wrapper) ;;
 *) echo 'usage: test-s14-mesa27-wayland-egl.sh zink [frames] [wrapper|turnip] | kgsl [frames] [none]' >&2;exit 2;;
esac
PIN="6f90e92dfdacc4759d8e9ccb328805d0f0b4439b"
SRC="$ROOT/.source/gpu-stage27-pr25/source"
[[ "$(git -C "$SRC" rev-parse HEAD)" == "$PIN" ]] || { echo 'S14_MESA27_PIN=INVALID' >&2;exit 2; }
RUNTIME="$ROOT/.source/gpu-stage27-pr25/mesa-private/private-runtime"
[[ -s "$RUNTIME/lib/libEGL_mesa.so" &&
   -s "$RUNTIME/lib/libgallium-27.0.0-devel.so" &&
   -s "$RUNTIME/egl_vendor.private.json" ]] || {
     echo 'S14_MESA27_PRIVATE_RUNTIME=NOT_BUILT' >&2;exit 2;
   }
BUILD="$ROOT/.source/gpu-stage27-pr25/mesa-private/build"
cmp -s "$BUILD/src/egl/libEGL_mesa.so" "$RUNTIME/lib/libEGL_mesa.so" || exit 2
cmp -s "$BUILD/src/gallium/targets/dri/libgallium-27.0.0-devel.so" \
  "$RUNTIME/lib/libgallium-27.0.0-devel.so" || exit 2
case "$icd" in
 wrapper)
  VK_DRIVER_FILES="$ROOT/.source/gpu-stage1/wrapper-private-runtime/wrapper_icd.private.json"
  [[ -s "$VK_DRIVER_FILES" ]] || exit 2
  export VK_DRIVER_FILES
  ;;
 turnip)
  VK_DRIVER_FILES="$RUNTIME/turnip_icd.private.json"
  [[ -s "$VK_DRIVER_FILES" ]] || {
    echo 'S14_TURNIP27_ICD=NOT_BUILT' >&2;exit 2;
  }
  export VK_DRIVER_FILES
  ;;
 none) unset VK_DRIVER_FILES;;
esac
if [[ "$mode" == zink ]]; then
  export MESA_LOADER_DRIVER_OVERRIDE=zink GALLIUM_DRIVER=zink
  export ALROOT_EGL_WAYLAND_KOPPER=1
else
  export MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno
  unset ALROOT_EGL_WAYLAND_KOPPER
fi
export __EGL_VENDOR_LIBRARY_FILENAMES="$RUNTIME/egl_vendor.private.json"
export LIBGL_DRIVERS_PATH="$RUNTIME/lib/dri"
export LD_LIBRARY_PATH="$RUNTIME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ALROOT_S12_PRIVATE_EGL=1
export ALROOT_PRIVATE_EGL_MAP_NEEDLE="/gpu-stage27-pr25/mesa-private/private-runtime/lib/libEGL_mesa.so"
if [[ "${ALROOT_S14_USE_KGSL_SHIM:-0}" == 1 ]]; then
  [[ "$mode" == kgsl ]] || exit 2
  # The 26.3 KGSL pixel test cannot authorize using a differently built
  # 27.0.0-devel driver: repeat the five-point same-AHB proof with this
  # exact private EGL/Gallium runtime, or refuse presentation.
  preflight="$ROOT/.source/gpu-stage27-pr25/results/kgsl27-pixel-preflight.log"
  mkdir -p "$(dirname "$preflight")"
  if ! ALROOT_S13_RESOLUTION=480x320 \
       ALROOT_S13_TEXTURE_STRIDE_WIDTH=1 \
       ALROOT_S13_TEXTURE_HEIGHT=270 \
       ALROOT_S13_SAMPLE_HEIGHT=270 \
       bash graphics/tools/test-s13-kgsl-ahb-capabilities.sh >"$preflight" 2>&1 ||
     ! grep -qx 'S13_KGSL_AHB_VENDOR_SAMPLE_POINTS=5/5' "$preflight"; then
       echo 'S14_MESA27_KGSL_AHB_PIXELS=NOT_VERIFIED' >&2
       tail -15 "$preflight" >&2
       exit 6
  fi
  echo 'S14_MESA27_KGSL_AHB_PIXELS=PASS_FIVE_CROSS_DRIVER_SAMPLES'
  shim="$ROOT/.source/gpu-stage2/kgsl/egl-shim/libalroot_kgsl_egl_ahb.so"
  [[ -s "$shim" ]] || { echo 'S14_KGSL_SHIM=NOT_BUILT' >&2;exit 2; }
  export LD_PRELOAD="$shim${LD_PRELOAD:+:$LD_PRELOAD}"
  export ALROOT_S13_KGSL_EGL_AHB=1
fi
TEMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s14-mesa27.XXXXXXXX")"
trap 'rm -rf "$TEMP"' EXIT
clang --target=aarch64-linux-android35 -std=c11 -Wall -Wextra -Werror \
 -Inative/waylandcomp/generated \
 graphics/tests/wayland-egl-swaps.c \
 native/waylandcomp/generated/xdg-shell-protocol.c \
 -lwayland-client -lwayland-egl -lEGL -lGLESv2 \
 -o "$TEMP/wayland-egl"
OUT="$ROOT/.source/gpu-stage27-pr25/results"
mkdir -p "$OUT"
LOG="$OUT/wayland-${mode}-${icd}-${frames}-shim${ALROOT_S14_USE_KGSL_SHIM:-0}.log"
stamp="$(date +'%m-%d %H:%M:%S.000')"
set +e
timeout -s TERM 24s env -u DISPLAY -u LIBGL_ALWAYS_SOFTWARE \
 WAYLAND_DEBUG=client \
 XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" WAYLAND_DISPLAY=wayland-0 EGL_PLATFORM=wayland \
 "$TEMP/wayland-egl" "$frames" >"$LOG" 2>&1
rc=$?
set -e
HOST="$OUT/host-${mode}-${icd}-${frames}-shim${ALROOT_S14_USE_KGSL_SHIM:-0}.log"
logcat -d -t "$stamp" -s AlrootWayland > "$HOST" 2>&1 || :
echo "S14_MESA27_TEST_DRIVER=$mode"
echo "S14_MESA27_TEST_VULKAN_ICD=$icd"
echo "S14_MESA27_PROCESS_RC=$rc"
grep -E '^S12_|^S13_EGL_SHIM' "$LOG" | tail -25 || :
echo "S14_MESA27_LOG=$LOG"
echo "S14_MESA27_HOST_LOG=$HOST"
if [[ "$rc" -ne 0 ]]; then tail -13 "$LOG";exit "$rc";fi
if ! grep -qx 'S12_PRIVATE_EGL_MAPPED=1' "$LOG" ||
   ! grep -E '^S12_GLES_VERSION=.*Mesa 27\.0\.0-devel' "$LOG"; then
  echo 'S14_MESA27_ACTIVE_DRIVER=WRONG_VERSION_OR_NOT_MAPPED' >&2
  exit 4
fi
echo 'S14_MESA27_ACTIVE_DRIVER=PINNED_27'
if [[ "$mode" == zink ]]; then
  grep -q '^S12_GLES_RENDERER=zink Vulkan' "$LOG" || exit 4
else
  grep -q '^S12_GLES_RENDERER=Adreno' "$LOG" || exit 4
fi
if grep -qx "S12_EGL_SWAP_SUCCESS_COUNT=$frames" "$LOG"; then
  echo 'S14_MESA27_EGL_SWAP=PASS'
else
  echo 'S14_MESA27_EGL_SWAP=FAIL'
  exit 5
fi
ahb="$(grep -Ec ' -> alroot_gpu_buffer_v1#[0-9]+\.create_buffer(_with_format)?\(' "$LOG" || true)"
shm="$(grep -Ec ' -> wl_shm_pool#[0-9]+\.create_buffer\(' "$LOG" || true)"
dma="$(grep -Ec ' -> zwp_linux_buffer_params_v1#[0-9]+\.create(_immed)?\(' "$LOG" || true)"
echo "S14_MESA27_WIRE_AHB_BUFFERS=$ahb"
echo "S14_MESA27_WIRE_SHM_BUFFERS=$shm"
echo "S14_MESA27_WIRE_DMABUF_BUFFERS=$dma"
if grep -q 'AHB -> VkImage GPU-copy:' "$HOST" &&
   grep -q '"Alroot S1.2 Wayland EGL/GLES swap test".*presenting GPU frames through Wayland' "$HOST"; then
  echo 'S14_MESA27_WAYLAND_GPU_COPY=HOST_IMPORT_CONFIRMED'
  [[ "$ahb" -gt 0 && "$shm" -eq 0 && "$dma" -eq 0 ]] || exit 5
else
  echo 'S14_MESA27_WAYLAND_GPU_COPY=NOT_CONFIRMED'
  if [[ "${ALROOT_S14_REQUIRE_GPU_COPY:-1}" == 1 ]]; then exit 5;fi
fi
