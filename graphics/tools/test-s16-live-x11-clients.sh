#!/data/data/com.termux/files/usr/bin/bash
# GPU-S1.6 regression: real Xwayland GLX and Vulkan XCB clients with explicitly
# selected, known-good S26U Adreno 840 backends. This does not install anything,
# restart the desktop, or alter global Termux/PRoot Mesa and Vulkan selection.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
STATE="$HOME/.alroot-wayland"
OUT="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/live-clients"
mkdir -p "$OUT"
[[ -r "$STATE/display" && -r "$STATE/Xauthority" ]] || { echo 'S16_LIVE_X11=NO_ALROOT_DISPLAY';exit 2; }
IFS= read -r D < "$STATE/display"
[[ "$D" =~ ^:[0-9]+$ && "$D" != :0 ]] || { echo "S16_LIVE_X11=UNSAFE_DISPLAY:$D";exit 2; }
[[ -s "$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" ]] || { echo 'S16_TURNIP_ICD=NOT_INSTALLED';exit 2; }
PID="$(awk 'NR==1{print $1}' "$STATE/xwayland.pid")"
[[ -r "/proc/$PID/cmdline" ]] || { echo S16_ALROOT_XWAYLAND_PROCESS=NOT_FOUND;exit 2; }
SERVER="$(tr '\0' ' ' <"/proc/$PID/cmdline")"
[[ "$SERVER" == *"/alroot_wayland/.source/gpu-stage1/xwayland-kgsl/"* ]] || {
    echo 'S16_LIVE_X11=SKIP_NON_KGSL_PRIVATE_XWAYLAND';exit 2;
}
export DISPLAY="$D" XAUTHORITY="$STATE/Xauthority"
unset WAYLAND_DISPLAY
printf 'S16_ALROOT_LIVE_DISPLAY=%s\n' "$D"
echo 'S16_DRIVER_SELECTION=SCOPED_TO_TEST_CHILDREN_ONLY'

run_test() {
    local label="$1" duration="$2";shift 2
    local rc=0
    timeout -s TERM -k 1s "$duration" "$@" >"$OUT/$label.log" 2>&1 || rc=$?
    echo "S16_LIVE_${label^^}_RC=$rc"
    case "$label" in
      *glxinfo*) grep -E 'direct rendering|OpenGL renderer string|OpenGL version string|OpenGL vendor string' "$OUT/$label.log" |tail -5 || : ;;
      *glmark*) grep -E 'GL_RENDERER|FPS:|Score:|error|failed' "$OUT/$label.log" |tail -7 || : ;;
      *vkcube*) head -4 "$OUT/$label.log" || : ;;
    esac
    if ((rc!=0));then echo "S16_LIVE_${label^^}=FAIL_OR_TIMEOUT";tail -8 "$OUT/$label.log";return 1;fi
}

run_test native_glxinfo 7s env \
   MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno FD_KGSL_ENABLE_DMABUF=1 \
   "$PREFIX/bin/glxinfo" -B
run_test native_glmark2 22s env \
   MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno FD_KGSL_ENABLE_DMABUF=1 \
   "$PREFIX/bin/glmark2" -b 'build:use-vbo=false'
run_test native_turnip_vkcube 9s env \
   VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" \
   "$PREFIX/bin/vkcube" --wsi xcb --c 20
run_test debian_glxinfo 12s env \
   MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno FD_KGSL_ENABLE_DMABUF=1 \
   "$PREFIX/bin/bash" "$ROOT/termux/aw-debian" /usr/bin/glxinfo -B

echo 'S16_LIVE_DRIVERS=PASS_EXPLICIT_SELECTION'
echo 'S16_LIVE_VISIBLE_VKCUBE_PIXELS=NOT_VERIFIED'
echo 'S16_LIVE_NATIVE_GLX_VULKAN_COMPATIBILITY=DRIVER_SPECIFIC'
