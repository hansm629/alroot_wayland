#!/data/data/com.termux/files/usr/bin/bash
# GPU-S1.2 vendor-neutral host AHB v2 / GLES / Zink Kopper acceptance.
# Does not install an APK, reset XFCE or change global Termux drivers.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
logs="$ROOT/.source/gpu-stage2/s12-acceptance"
mkdir -p "$logs"
start="$(date +'%m-%d %H:%M:%S.000')"

echo 'S1.2: verify the active host advertises optional BGRA8 AHB protocol v2'
caps="$(bash graphics/tools/check-stage1-accel.sh)"
echo "$caps" | grep -E 'ALROOT_GPU_AHB_ONLY_(VERSION|AVAILABLE)='
if ! grep -qx 'ALROOT_GPU_AHB_ONLY_VERSION=2' <<< "$caps"; then
    echo 'FAIL: the current host is still RGBA8-only v1. Install S1.2 test APK first.' >&2
    exit 2
fi

echo 'S1.2: separate Vulkan ICD AHB RGBA8/BGRA8 memory capabilities'
bash graphics/tools/test-s12-ahb-formats.sh > "$logs/ahb-formats.log" 2>&1
grep -E '^S12_AHB_' "$logs/ahb-formats.log"

echo 'S1.2: system Android GLES -> RGBA8 AHB -> Vulkan host'
bash graphics/tools/test-s12-android-gles-ahb.sh 90 rgba8 |
    tee "$logs/android-gles-rgba8.log"

echo 'S1.2: optional Android GLES BGRA8 producer'
if bash graphics/tools/test-s12-android-gles-ahb.sh 45 bgra8 |
    tee "$logs/android-gles-bgra8.log"; then
    echo 'S12_ANDROID_GLES_BGRA8=PASS'
else
    echo 'S12_ANDROID_GLES_BGRA8=NOT_VERIFIED'
    echo 'Optional: EGL BGRA8 FBO support differs among Android GPU vendors.'
fi

echo 'S1.2: Termux Mesa Zink -> Kopper -> wrapper BGRA8 AHB WSI'
ALROOT_S12_PRIVATE_EGL=1 ALROOT_S12_REQUIRE_GPU_COPY=1 \
    bash graphics/tools/test-s12-wayland-egl.sh zink 90 |
    tee "$logs/zink-kopper-ahb.log"

echo 'S1.2: prove Zink/Kopper used the AHB protocol, not wl_shm'
ALROOT_S12_PRIVATE_EGL=1 ALROOT_S12_REQUIRE_GPU_COPY=1 \
    ALROOT_S12_WIRE_VERIFY=1 \
    bash graphics/tools/test-s12-wayland-egl.sh zink 6 |
    tee "$logs/zink-kopper-wire.log"

echo 'S1.2: protect prior Vulkan and Termux-X11 DRI3 paths'
bash graphics/tools/test-vkcube-stage1.sh fifo 120 |
    tee "$logs/vkcube-regression.log"
bash graphics/tools/snapshot-termux-x11.sh verify |
    tail -3

logcat -d -t "$start" -s AlrootWayland > "$logs/host.log" 2>&1 || :
if grep -E 'S1.1 AHB buffer accounting underflow|Vulkan device lost' "$logs/host.log"; then
    echo 'FAIL: host AHB lifetime underflow or Vulkan device loss' >&2
    exit 5
fi
echo 'S12_GPU_COPY_CLIENT_HOST_ACCEPTANCE=PASS'
echo 'VISUAL_ACCEPTANCE=USER_CHECK_REQUIRED'
echo "S12_LOGS=$logs"
