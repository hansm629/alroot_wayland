#!/data/data/com.termux/files/usr/bin/bash
# Launch a diagnostic client on the LIVE Alroot Xwayland instead of
# inheriting the caller Termux shell's unrelated DISPLAY=:0.
# No system-wide Mesa/Vulkan/Termux environment or desktop profile mutation.
set -euo pipefail
PREFIX=/data/data/com.termux/files/usr
HOME=/data/data/com.termux/files/home
STATE="$HOME/.alroot-wayland"
if (($# == 0)); then
    echo 'usage: bash graphics/tools/run-s16-x11-client.sh glxinfo -B | glmark2 ... | vkcube ... | debian-glxinfo' >&2
    exit 2
fi
[[ -r "$STATE/display" && -r "$STATE/Xauthority" && -r "$STATE/xwayland.pid" ]] || {
    echo 'S16_RUN_BLOCKED=NO_ALROOT_X11_SESSION' >&2;exit 2;
}
IFS= read -r display < "$STATE/display"
[[ "$display" =~ ^:[0-9]{1,3}$ && "$display" != :0 ]] || {
    echo 'S16_RUN_BLOCKED=UNSAFE_X_DISPLAY' >&2;exit 2;
}
read -r pid ticks marker < "$STATE/xwayland.pid"
[[ "$pid" =~ ^[0-9]+$ && "$marker" == alroot-wayland && -r "/proc/$pid/cmdline" ]] || {
    echo 'S16_RUN_BLOCKED=SERVER_MARKER_MISSING' >&2;exit 2;
}
cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline")"
[[ "$cmdline" == *"Xwayland $display "* && "$cmdline" == *"$HOME/alroot_wayland/.source/gpu-stage1/xwayland-kgsl/"* ]] || {
    echo 'S16_RUN_BLOCKED=NO_PRIVATE_KGSL_XWAYLAND' >&2;exit 2;
}
export DISPLAY="$display" XAUTHORITY="$STATE/Xauthority"
unset WAYLAND_DISPLAY
case "${1##*/}" in
    glxinfo|glxgears|glmark2)
        export MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno FD_KGSL_ENABLE_DMABUF=1
        exec "$@" ;;
    vkcube)
        [[ -r "$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" ]] || {
            echo 'S16_RUN_BLOCKED=NO_NATIVE_TURNIP_ICD' >&2;exit 2;
        }
        export VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json"
        exec "$@" ;;
    debian-glxinfo)
        shift
        export MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno FD_KGSL_ENABLE_DMABUF=1
        exec "$PREFIX/bin/bash" "$HOME/alroot_wayland/termux/aw-debian" /usr/bin/glxinfo "$@" ;;
    *) echo 'S16_RUN_BLOCKED=UNSUPPORTED_CLIENT (only glxinfo, glxgears, glmark2, vkcube, debian-glxinfo)' >&2;exit 2 ;;
esac
