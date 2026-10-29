#!/data/data/com.termux/files/usr/bin/bash
# Real gralloc AHB -> Turnip27 Vulkan external DMA-BUF memory -> GPU fill ->
# vendor Android EGL verifies five pixels. No WSI/SHM/server mutation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
DIR="$ROOT/.source/gpu-stage27-pr25"
[[ "$(git -C "$DIR/source" rev-parse HEAD)" == \
  6f90e92dfdacc4759d8e9ccb328805d0f0b4439b ]] || exit 2
RUNTIME="$DIR/mesa-private/private-runtime"
[[ -s "$RUNTIME/lib/libvulkan_freedreno.so" ]] || exit 2
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s15-turnip.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
  -Igraphics/include graphics/tests/s15-turnip-ahb-buffer-import.c \
  graphics/src/kgsl_ahb_fd.c \
  -lvulkan -lEGL -lGLESv2 -ldl -o "$TMP/turnip-ahb-probe"
if [[ -n "${ALROOT_S15_TEST_ICD:-}" ]]; then
  S15_ICD="$DIR/s15-turnip/private-runtime/turnip_alroot_icd.private.json"
  [[ "$ALROOT_S15_TEST_ICD" == "$S15_ICD" && -s "$S15_ICD" ]] || {
    echo 'S15_AHB_TEST_ICD=INVALID_PRIVATE_PATH' >&2
    exit 2
  }
  export VK_DRIVER_FILES="$S15_ICD"
  export LD_LIBRARY_PATH="$DIR/s15-turnip/private-runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  echo 'S15_AHB_TEST_ICD=TURNIP27_PRIVATE_ALROOT_WSI'
else
  export VK_DRIVER_FILES="$RUNTIME/turnip_icd.private.json"
  export LD_LIBRARY_PATH="$RUNTIME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  echo 'S15_AHB_TEST_ICD=TURNIP27_PINNED_BASELINE'
fi
OUT="$DIR/s15-results"
mkdir -p "$OUT"
LOG="$OUT/turnip-ahb-buffer-gpu-fill.log"
set +e
timeout -s TERM 12s "$TMP/turnip-ahb-probe" >"$LOG" 2>&1
rc=$?
set -e
cat "$LOG"
echo "S15_TURNIP_AHB_BUFFER_PROBE_RC=$rc"
if ((rc!=0)) || ! grep -qx 'S15_TURNIP_AHB_VENDOR_SAMPLE_POINTS=5/5' "$LOG" ||
   ! grep -q '^S15_TURNIP_AHB_CROSS_DRIVER_PIXELS=PASS' "$LOG"; then
    echo 'S15_TURNIP_AHB_BUFFER_GPU_WRITE=NOT_CONFIRMED'
    exit 5
fi
echo 'S15_TURNIP_AHB_BUFFER_GPU_WRITE=PASS'
