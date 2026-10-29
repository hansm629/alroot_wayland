#!/data/data/com.termux/files/usr/bin/bash
# Experimental S1.3-A KGSL AHB producer: physical pixels + live display + regressions.
# No helper APK install, driver replacement, Termux-X11 restart or XFCE reset.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage2/kgsl/s13-acceptance"
mkdir -p "$OUT"
timestamp="$(date +'%m-%d %H:%M:%S.000')"

printf '%s\n' 'S1.3-A: verify AHB transport exists (RGBA8 works with protocol v1/v2)'
caps="$(bash graphics/tools/check-stage1-accel.sh)"
grep '^ALROOT_GPU_AHB_ONLY_' <<< "$caps"
grep -qx 'ALROOT_GPU_AHB_ONLY_AVAILABLE=1' <<< "$caps"

echo 'S1.3-A: physical KGSL -> AHB -> vendor EGL pixel identity (5 points)'
ALROOT_S13_RESOLUTION=480x320 \
  ALROOT_S13_TEXTURE_STRIDE_WIDTH=1 \
  ALROOT_S13_TEXTURE_HEIGHT=270 \
  ALROOT_S13_SAMPLE_HEIGHT=270 \
  bash graphics/tools/test-s13-kgsl-ahb-capabilities.sh > "$OUT/kgsl-ahb-pixels.log" 2>&1
grep -E '^S13_(AHB_DIMENSIONS|KGSL_TEXTURE_DIMENSIONS|KGSL_AHB_VENDOR_SAMPLE_POINTS|KGSL_AHB_PIXEL_SHARING)=' "$OUT/kgsl-ahb-pixels.log"
grep -qx 'S13_KGSL_AHB_VENDOR_SAMPLE_POINTS=5/5' "$OUT/kgsl-ahb-pixels.log"
grep -qx 'S13_KGSL_AHB_PIXEL_SHARING=PASS' "$OUT/kgsl-ahb-pixels.log"

echo 'S1.3-A: KGSL direct AHB -> Wayland Vulkan host 90 frames'
bash graphics/tools/test-s13-kgsl-ahb-window.sh 90 | tee "$OUT/kgsl-ahb-90.log"
grep -qx 'S13_KGSL_AHB_FRAMES=90 requested=90' "$OUT/kgsl-ahb-90.log"
grep -qx 'S13_KGSL_AHB_GPU_COPY=HOST_IMPORT_CONFIRMED' "$OUT/kgsl-ahb-90.log"

echo 'S1.3-A: assert Wayland AHB protocol (no wl_shm / linux-dmabuf fallback)'
ALROOT_S13_WIRE_VERIFY=1 \
  bash graphics/tools/test-s13-kgsl-ahb-window.sh 6 | tee "$OUT/kgsl-ahb-wire.log"
grep -qx 'S13_KGSL_WIRE_GPU_TRANSPORT=AHB_ONLY' "$OUT/kgsl-ahb-wire.log"

echo 'S1.3-A: protect prior private Zink/Kopper and Vulkan paths'
ALROOT_S12_PRIVATE_EGL=1 ALROOT_S12_REQUIRE_GPU_COPY=1 \
  bash graphics/tools/test-s12-wayland-egl.sh zink 12 | tee "$OUT/zink-regression.log"
grep -qx 'S12_EGL_WAYLAND_GPU_COPY=HOST_IMPORT_CONFIRMED' "$OUT/zink-regression.log"
bash graphics/tools/test-vkcube-stage1.sh fifo 60 | tee "$OUT/vkcube-regression.log"

echo 'S1.3-A: protect existing global Termux-X11 DRI3 and installed drivers'
bash graphics/tools/snapshot-termux-x11.sh verify > "$OUT/termux-x11-integrity.log"
tail -3 "$OUT/termux-x11-integrity.log"

logcat -d -t "$timestamp" -s AlrootWayland > "$OUT/host.log" 2>&1 || :
if grep -E 'AHB buffer accounting underflow|Vulkan device lost' "$OUT/host.log"; then
    echo 'S13_KGSL_HOST_STABILITY=FAIL' >&2
    exit 5
fi
echo 'S13_KGSL_AHB_PRODUCER_ACCEPTANCE=PASS'
echo 'S13_STANDARD_KGSL_EGL_WAYLAND=NOT_IMPLEMENTED'
echo 'VISUAL_ACCEPTANCE=USER_CHECK_REQUIRED'
echo "S13_LOGS=$OUT"
