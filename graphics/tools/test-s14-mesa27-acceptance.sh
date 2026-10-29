#!/data/data/com.termux/files/usr/bin/bash
# Full pinned Mesa 27 validation report. Missing support remains NOT_CONFIRMED.
# No APK change, package install, Wayland/Xwayland server restart, driver replacement.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
BASE="$ROOT/.source/gpu-stage27-pr25"
OUT="$BASE/acceptance"
mkdir -p "$OUT"
bash graphics/tools/check-s14-mesa27-pr25.sh > "$OUT/provenance.log"
sha256sum -c graphics/patches/mesa/pr25/SHA256SUMS > "$OUT/packaging-patches.log"
echo 'S14_MESA27_PROVENANCE=PINNED_6F90E92_PR25_14_PATCHES'

run_case() {
  label="$1"; shift
  echo "S14_CASE_START=$label"
  if "$@" > "$OUT/$label.log" 2>&1; then
    echo "S14_CASE_PROCESS_${label}=EXIT_0"
  else
    rc=$?
    echo "S14_CASE_PROCESS_${label}=EXIT_$rc"
  fi
  grep -E '^S14_|^S12_GLES_RENDERER=|^S12_GLES_VERSION=|^S13_EGL_SHIM_STATS' \
    "$OUT/$label.log" | tail -20 || :
}

run_case zink27_wrapper_wayland bash graphics/tools/test-s14-mesa27-wayland-egl.sh zink 90 wrapper
run_case zink27_turnip27_wayland bash graphics/tools/test-s14-mesa27-wayland-egl.sh zink 90 turnip
run_case kgsl27_wayland_direct bash graphics/tools/test-s14-mesa27-wayland-egl.sh kgsl 24 none
run_case kgsl27_wayland_ahb env ALROOT_S14_USE_KGSL_SHIM=1 \
  bash graphics/tools/test-s14-mesa27-wayland-egl.sh kgsl 90 none
run_case turnip27_vulkan_wayland bash graphics/tools/test-s14-mesa27-turnip.sh wayland 60
run_case turnip27_vulkan_xcb bash graphics/tools/test-s14-mesa27-turnip.sh xcb 60
run_case xwayland90 env ALROOT_S14_XWAYLAND_DISPLAY=:90 \
  bash graphics/tools/test-s14-mesa27-xwayland.sh
run_case xwayland0 env ALROOT_S14_XWAYLAND_DISPLAY=:0 \
  bash graphics/tools/test-s14-mesa27-xwayland.sh

if sha256sum -c .source/gpu-audit/termux-x11-protected.sha256 \
  > "$OUT/system-renderer-hashes.log" 2>&1; then
 echo 'S14_SYSTEM_TERMUX_MESA26=UNCHANGED'
else
 echo 'S14_SYSTEM_TERMUX_MESA26=HASH_MISMATCH'
fi
if [[ -z "$(git -C "$BASE/source" status --porcelain)" ]]; then
 echo 'S14_PINNED_MESA27_REF=UNCHANGED'
else
 echo 'S14_PINNED_MESA27_REF=CHANGED_UNEXPECTEDLY'
fi

# A matrix run is not all-PASS acceptance. Only mark the independently
# confirmed transport successes. Missing Turnip WSI or server DRI3 stays
# a blocking item until it has its own live client/host proof.
if grep -qx 'S14_MESA27_WAYLAND_GPU_COPY=HOST_IMPORT_CONFIRMED' \
    "$OUT/zink27_wrapper_wayland.log" &&
   grep -qx 'S14_MESA27_WAYLAND_GPU_COPY=HOST_IMPORT_CONFIRMED' \
    "$OUT/kgsl27_wayland_ahb.log"; then
  echo 'S14_MESA27_PINNED_WAYLAND_AHB_BASELINE=PASS_ZINK_WRAPPER_AND_KGSL_SHIM'
else
  echo 'S14_MESA27_PINNED_WAYLAND_AHB_BASELINE=INCOMPLETE'
fi
if grep -q 'S14_TURNIP27_WAYLAND_DMA_BUF_IMPORT=INVALID_EXTERNAL_HANDLE' \
    "$OUT/turnip27_vulkan_wayland.log"; then
  echo 'S14_TURNIP27_ALROOT_AHB_WSI=NOT_IMPLEMENTED_DMA_BUF_REJECTED'
else
  echo 'S14_TURNIP27_ALROOT_AHB_WSI=NOT_CONFIRMED'
fi
if grep -qx 'S14_XWAYLAND_DRI3=0' "$OUT/xwayland90.log"; then
  echo 'S14_XWAYLAND_DRI3=BLOCKED_NOT_ADVERTISED'
else
  echo 'S14_XWAYLAND_DRI3=REQUIRES_LIVE_PRESENT_PROOF'
fi
echo 'S14_XWAYLAND_GLAMOR_GPU_PRESENT=NOT_VERIFIED'
echo 'S14_MESA27_OVERALL=PARTIAL_VALIDATION_NOT_GENERAL_WAYLAND_XWAYLAND_SUPPORT'
echo 'S14_END_TO_END_VISUAL=USER_CHECK_REQUIRED'
echo "S14_ACCEPTANCE_LOGS=$OUT"
