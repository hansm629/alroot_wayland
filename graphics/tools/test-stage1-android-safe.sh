#!/data/data/com.termux/files/usr/bin/bash
# Safe S26U/S24+ smoke collection; allocates tiny temporary GPU-only AHBs.
# No APK install, shell-wide EGL/ICD override, active X display, or reboots.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"

echo '=== STAGE 1: policy, host-patch and protocol contracts ==='
bash graphics/tools/test-stage1-inert.sh
echo '=== STAGE 2: actual Android EGL/GLES loader ==='
bash graphics/tools/probe-android-egl.sh
echo '=== STAGE 3: Android EGL -> renderable AHB ==='
bash graphics/tools/probe-android-ahb-egl.sh
echo '=== STAGE 4: separate process AHB -> system Vulkan VkImage import ==='
bash graphics/tools/test-ahb-vulkan-ipc.sh
echo '=== STAGE 5: private headless Xwayland startup ==='
if [[ -x .source/gpu-stage1/xwayland/build/hw/xwayland/Xwayland ]]; then
    python3 graphics/tools/test-xwayland-private.py
else
    echo 'SKIP private Xwayland: run graphics/tools/build-xwayland-private.sh first'
fi
echo '=== FINAL SAFETY GATE ==='
bash graphics/tools/snapshot-termux-x11.sh verify
echo 'PASS: all available safe Stage-1 diagnostics'
echo 'IMPORTANT: actual Wayland/Xwayland GPU pixels + fence + screen present NOT TESTED'
