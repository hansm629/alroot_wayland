#!/data/data/com.termux/files/usr/bin/bash
# S1.5 private Mesa 27 Turnip AHB WSI regression matrix.
# Success is not based on a client swap/exit alone; host AHB VkImage import
# and strict AHB-only wire evidence are required for both native Vulkan and
# Zink. Existing S1.4 private EGL and Termux system drivers stay unchanged.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
BASE="$ROOT/.source/gpu-stage27-pr25"
OUT="$BASE/s15-acceptance"
mkdir -p "$OUT"
PIN=6f90e92dfdacc4759d8e9ccb328805d0f0b4439b
[[ "$(git -C "$BASE/source" rev-parse HEAD)" == "$PIN" ]] || exit 2
[[ -z "$(git -C "$BASE/source" status --porcelain)" ]] || exit 2
sha256sum -c graphics/patches/mesa/pr25/SHA256SUMS > "$OUT/pinned-package-patches.log"
echo 'S15_MESA27_SOURCE=LFDEVS_PR25_6F90E92'

case_run() {
   local tag="$1"; shift
   echo "S15_CASE_START=$tag"
   if "$@" >"$OUT/$tag.log" 2>&1; then
      echo "S15_CASE_$tag=PASS"
   else
      local rc=$?
      echo "S15_CASE_$tag=FAIL_$rc"
      grep -E '^S15_|^S14_|^S12_|FAILED|VK_ERROR|error:' "$OUT/$tag.log" | tail -20 || :
      return "$rc"
   fi
   grep -E '^S15_|^S14_MESA27_WAYLAND_GPU_COPY' "$OUT/$tag.log" | tail -20 || :
}

# New Vulkan ICD: the driver must itself emit AHB and obtain host import.
case_run turnip_vulkan_fifo_60 \
   bash graphics/tools/test-s15-turnip-wayland-wsi.sh 60
case_run turnip_vulkan_mailbox_60 \
   env ALROOT_S15_PRESENT_MODE=1 \
   bash graphics/tools/test-s15-turnip-wayland-wsi.sh 60
# Zink 27 / Kopper reuses the new WSI; it must not secretly select wrapper.
case_run zink_turnip_60 \
   bash graphics/tools/test-s15-zink27-turnip-ahb.sh 60
# Regression: known-good private Vulkan wrapper and KGSL AHB shim.
case_run s14_zink_wrapper_12 \
   bash graphics/tools/test-s14-mesa27-wayland-egl.sh zink 12 wrapper
case_run s14_kgsl_shim_12 \
   env ALROOT_S14_USE_KGSL_SHIM=1 \
   bash graphics/tools/test-s14-mesa27-wayland-egl.sh kgsl 12 none

sha256sum -c .source/gpu-audit/termux-x11-protected.sha256 \
   >"$OUT/system-renderer-hashes.log"
[[ -z "$(git -C "$BASE/source" status --porcelain)" ]] || exit 2
echo 'S15_TERMUX_MESA26_AND_PINNED_SOURCE=UNCHANGED'
echo 'S15_TURNIP_WAYLAND_AHB_GPU_COPY_ACCEPTANCE=PASS'
echo 'S15_CPU_FENCE_REMOVAL=NOT_IMPLEMENTED'
echo 'S15_ZERO_COPY=NOT_CLAIMED'
echo 'S15_VISUAL=USER_CHECK_REQUIRED'
echo "S15_ACCEPTANCE_LOGS=$OUT"
