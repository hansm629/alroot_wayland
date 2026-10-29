#!/data/data/com.termux/files/usr/bin/bash
# End-to-end guarded S1.3 experimental checks, no global Mesa changes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
LOG="$ROOT/.source/gpu-stage2/kgsl/s13b-acceptance"
mkdir -p "$LOG"
echo 'S1.3-B: direct KGSL AHB 5-point pixels, lifecycle, Zink/Vulkan/DRI3 regression'
bash graphics/tools/test-s13-acceptance.sh | tee "$LOG/s13a.log"
echo 'S1.3-B: normal EGL Wayland client with private KGSL AHB shim (90 frames)'
bash graphics/tools/test-s13-kgsl-egl-shim.sh 90 | tee "$LOG/egl-shim90.log"
echo 'S1.3-B: longer EGL Wayland lifecycle and Wayland wire (360 frames)'
bash graphics/tools/test-s13-kgsl-egl-shim.sh 360 | tee "$LOG/egl-shim360.log"
for f in "$LOG/egl-shim90.log" "$LOG/egl-shim360.log"; do
    grep -qx 'S13_KGSL_EGL_SHIM_GPU_COPY=HOST_IMPORT_CONFIRMED' "$f"
    grep -qx 'S13_KGSL_EGL_SHIM_FRAMES_AND_RELEASE=PASS' "$f"
    grep -qx 'S13_KGSL_EGL_WIRE_SHM_BUFFERS=0' "$f"
    grep -qx 'S13_KGSL_EGL_WIRE_DMA_BUFFERS=0' "$f"
done
echo 'S13_KGSL_EGL_EXPERIMENTAL_ACCEPTANCE=PASS'
echo 'S13_KGSL_GENERAL_EGL_SUPPORT=NOT_IMPLEMENTED'
echo 'S13_KGSL_XWAYLAND_GLAMOR=NOT_IMPLEMENTED'
echo 'VISUAL_ACCEPTANCE=USER_CHECK_REQUIRED'
echo "S13B_LOGS=$LOG"
