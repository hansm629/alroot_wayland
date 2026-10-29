#!/data/data/com.termux/files/usr/bin/bash
# Optional visible Vulkan WSI acceptance against the current Alroot host.
# Does NOT restart/replace any APK, compositor, installed ICD or X server.
# The user should also verify actual on-screen pixels; logs alone do not
# guarantee a visually correct texture or frame.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
ICD="$ROOT/.source/gpu-stage1/wrapper-private-runtime/wrapper_icd.private.json"
OUT="$ROOT/.source/gpu-stage1/repro-vkcube"
mkdir -p "$OUT"
if [[ ! -s "$ICD" ]]; then
    echo 'Private Vulkan wrapper is unavailable. Build it first:' >&2
    echo 'ALROOT_BUILD_WRAPPER_FULL=1 ALROOT_VERIFY_WRAPPER=1 bash graphics/tools/build-wrapper-wayland-private.sh' >&2
    exit 2
fi
if [[ ! -S "$HOME/.alroot-wayland/runtime/wayland-0" ]]; then
    echo 'Alroot Wayland session is not active' >&2
    exit 2
fi
# Materialize the full capability output before grepping. With pipefail,
# grep -q can close its pipe early, SIGPIPE the still-running probe, and
# incorrectly mark a working host as unavailable.
capabilities="$(bash graphics/tools/check-stage1-accel.sh)"
if ! grep -qx 'ALROOT_GPU_AHB_ONLY_AVAILABLE=1' <<< "$capabilities"; then
    echo 'Active host does not advertise the Alroot AHB-only protocol' >&2
    exit 2
fi
mode="${1:-fifo}"
frames="${2:-120}"
case "$mode" in
fifo) present_mode=2 ;;
mailbox) present_mode=1 ;;
*) echo 'usage: test-vkcube-stage1.sh fifo|mailbox [framecount]' >&2; exit 2 ;;
esac
[[ "$frames" =~ ^[0-9]+$ ]] && (( frames >= 1 && frames <= 900 )) || {
    echo 'framecount must be between 1 and 900' >&2
    exit 2
}
timestamp="$(date +'%m-%d %H:%M:%S.000')"
log="$OUT/acceptance-${mode}-${frames}.log"
host="$OUT/host-${mode}-${frames}.log"
echo "GPU Stage 1: private Vulkan wrapper, $mode, $frames frames"
set +e
timeout -s TERM 18s env -u DISPLAY \
    XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime" \
    WAYLAND_DISPLAY=wayland-0 \
    VK_DRIVER_FILES="$ICD" \
    WRAPPER_SURFACE_FORMAT=rgba8 \
    vkcube --wsi wayland --width 640 --height 360 \
    --present_mode "$present_mode" --c "$frames" > "$log" 2>&1
result=$?
set -e
logcat -d -t "$timestamp" -s AlrootWayland > "$host" 2>&1 || :
echo "vkcube exit code: $result"
grep -m1 'Selected GPU' "$log" || :
if (( result != 0 )); then
    tail -30 "$log"
    echo 'FAIL: vkcube failed or timed out. See:' "$log" >&2
    exit 1
fi
if ! grep -q 'AHB -> VkImage GPU-copy:' "$host" ||
   ! grep -q '"vkcube".*presenting GPU frames through Wayland' "$host"; then
    echo 'UNVERIFIED: vkcube exited but current host AHB import/present logs are missing' >&2
    tail -25 "$host"
    exit 1
fi
echo 'PASS: client completed requested frames without reported errors'
echo 'PASS: host imported AHB to Vulkan VkImage and opened a vkcube surface'
echo 'VISUAL_ACCEPTANCE=USER_CHECK_REQUIRED'
echo "Client log: $log"
echo "Host log: $host"
