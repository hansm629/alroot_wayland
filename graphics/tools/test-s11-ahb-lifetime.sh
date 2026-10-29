#!/data/data/com.termux/files/usr/bin/bash
# Real host/client wl_buffer lifetime regression; read-only to server config.
# Uses Android EGL or the existing PRIVATE Vulkan wrapper. Three unique AHB
# sizes, exactly-once release per replacement/detach, then repeated reuse.
# Does not change Termux-X11 DRI3, driver files, APK, XFCE or keyboard.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
renderer="${1:-egl}"
cycles="${2:-12}"
[[ "$cycles" =~ ^[0-9]+$ ]] && (( cycles>=1 && cycles<=64 )) || {
    echo 'usage: test-s11-ahb-lifetime.sh egl|vulkan [1..64 cycles]' >&2
    exit 2
}
echo "GPU-S1.1 lifetime: renderer=$renderer cycles=$cycles"
export ALROOT_GPU_S11_STRESS=1 ALROOT_GPU_S11_CYCLES="$cycles"
export ALROOT_GPU_S11_DESTROY_ATTACHED=1
case "$renderer" in
egl)
    timeout -s TERM 35s env -u DISPLAY \
        XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
        WAYLAND_DISPLAY=wayland-0 \
        bash graphics/tools/test-wayland-ahb-window.sh egl
    ;;
vulkan)
    timeout -s TERM 35s env -u DISPLAY \
        XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
        WAYLAND_DISPLAY=wayland-0 \
        bash graphics/tools/test-wayland-ahb-window.sh vulkan wrapper-private
    ;;
*)
    echo 'usage: test-s11-ahb-lifetime.sh egl|vulkan [cycles]' >&2
    exit 2
    ;;
esac
