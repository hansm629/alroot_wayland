#!/data/data/com.termux/files/usr/bin/bash
# Read-only per-session GPU acceleration inventory, independent of desktop
# environment (XFCE4, KDE Plasma, LXQt, GNOME, or other Wayland clients).
# No XTEST injection, server restart, package/driver replacement or forcing
# of a particular GPU. This is NOT a performance benchmark.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-stage1-check.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=c11 -Wall -Wextra -Werror \
    graphics/tools/probe-wayland-gpu-global.c -lwayland-client \
    -o "$TMP/wayland"
env -u DISPLAY XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
    WAYLAND_DISPLAY=wayland-0 "$TMP/wayland"
display="${ALROOT_TEST_DISPLAY:-}"
if [[ -z "$display" && -r "$HOME/.alroot-wayland/display" ]]; then
    read -r display < "$HOME/.alroot-wayland/display" || true
fi
if [[ -z "$display" ]]; then
    echo 'ALROOT_GPU_XWAYLAND=NO_DISPLAY'
else
    echo "ALROOT_GPU_XWAYLAND_DISPLAY=$display"
    if DISPLAY="$display" XAUTHORITY="$HOME/.alroot-wayland/Xauthority" \
        timeout 4 xdpyinfo -queryExtensions > "$TMP/ext" 2>&1; then
        for ext in DRI3 GLX Present; do
            if grep -Eq "^[[:space:]]+$ext[[:space:]]+[(]" "$TMP/ext"; then
                echo "ALROOT_GPU_XWAYLAND_${ext}=1"
            else
                echo "ALROOT_GPU_XWAYLAND_${ext}=0"
            fi
        done
    else
        echo 'ALROOT_GPU_XWAYLAND=NO_RESPONSE'
    fi
fi
echo 'ALROOT_GPU_GLAMOR_FRAME_PRESENT=NOT_VERIFIED'
echo 'ALROOT_GPU_NATIVE_VULKAN_FRAME_PRESENT=NOT_VERIFIED'
echo 'ALROOT_GPU_DESKTOP_COMPOSITION=NOT_VERIFIED'
echo 'NOTE: GLX/DRI3 extension visibility is not proof of GPU buffer import.'
