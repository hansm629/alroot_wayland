#!/data/data/com.termux/files/usr/bin/bash
# Verify ONLY the pinned Mesa 27 Turnip ICD, not installed Termux Turnip.
# Native Vulkan instance enumeration is distinct from actual Wayland display.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
mode="${1:-wayland}"
frames="${2:-60}"
case "$mode" in wayland|xcb) ;; *) echo 'usage: test-s14-mesa27-turnip.sh wayland|xcb [1..360]' >&2;exit 2;; esac
[[ "$frames" =~ ^[0-9]+$ ]] && ((frames>=1&&frames<=360)) || exit 2
BASE="$ROOT/.source/gpu-stage27-pr25"
PIN="6f90e92dfdacc4759d8e9ccb328805d0f0b4439b"
[[ "$(git -C "$BASE/source" rev-parse HEAD)" == "$PIN" ]] || exit 2
RUNTIME="$BASE/mesa-private/private-runtime"
ICD="$RUNTIME/turnip_icd.private.json"
[[ -s "$ICD" && -s "$RUNTIME/lib/libvulkan_freedreno.so" ]] || {
 echo 'S14_TURNIP27_RUNTIME=NOT_BUILT' >&2;exit 2;
}
OUT="$BASE/results"
mkdir -p "$OUT"
export VK_DRIVER_FILES="$ICD"
export LD_LIBRARY_PATH="$RUNTIME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
echo "S14_TURNIP27_PIN=$PIN"
echo "S14_TURNIP27_ICD=$ICD"
info="$OUT/turnip27-vulkaninfo.log"
set +e
timeout 14s vulkaninfo --summary >"$info" 2>&1
info_rc=$?
set -e
echo "S14_TURNIP27_VULKANINFO_EXIT=$info_rc"
grep -E 'deviceName|driverName|driverInfo|apiVersion|GPU[0-9]|ERROR|error:' "$info" | tail -20 || :
if ((info_rc!=0)) || ! grep -q 'deviceName.*Adreno' "$info"; then
 echo 'S14_TURNIP27_GPU_ENUMERATION=FAIL'
 exit 4
fi
echo 'S14_TURNIP27_GPU_ENUMERATION=PASS'
if [[ "$mode" == wayland ]]; then
  export XDG_RUNTIME_DIR="$HOME/.alroot-wayland/runtime"
  export WAYLAND_DISPLAY=wayland-0
  unset DISPLAY
else
  export DISPLAY=:90
  export XAUTHORITY="$HOME/.alroot-wayland/Xauthority"
fi
stamp="$(date +'%m-%d %H:%M:%S.000')"
log="$OUT/turnip27-vkcube-$mode-$frames.log"
set +e
timeout -s TERM 16s vkcube --wsi "$mode" --c "$frames" \
 --width 640 --height 360 --present_mode 2 >"$log" 2>&1
rc=$?
set -e
echo "S14_TURNIP27_VKCUBE_WSI=$mode"
echo "S14_TURNIP27_VKCUBE_EXIT=$rc"
grep -E 'Selected GPU|error|ERROR|FATAL|VK_ERROR|Failed|failed|DRI3' "$log" | tail -20 || :
echo "S14_TURNIP27_VKCUBE_LOG=$log"
if [[ "$mode" == wayland ]]; then
  host="$OUT/turnip27-vkcube-host-$frames.log"
  logcat -d -t "$stamp" -s AlrootWayland > "$host" 2>&1 || :
  if grep -q 'AHB -> VkImage GPU-copy:' "$host" &&
     grep -Ei 'vkcube.*presenting GPU frames through Wayland' "$host"; then
    echo 'S14_TURNIP27_WAYLAND_GPU_COPY=HOST_IMPORT_CONFIRMED'
  else
    echo 'S14_TURNIP27_WAYLAND_GPU_COPY=NOT_CONFIRMED'
    if grep -q 'dmabuf.*INVALID_EXTERNAL_HANDLE' "$host"; then
      echo 'S14_TURNIP27_WAYLAND_DMA_BUF_IMPORT=INVALID_EXTERNAL_HANDLE'
    fi
    grep -Ei 'failed|dmabuf|import|vkcube' "$host" | tail -12 || :
    echo 'S14_TURNIP27_WAYLAND_FRAME_VISIBLE=NOT_VERIFIED'
    # The Vulkan client may exit 0 while every submitted GPU image is
    # rejected by the host. Never treat that as a successful display.
    exit 5
  fi
else
  echo 'S14_TURNIP27_XWAYLAND_GLAMOR_GPU=NOT_VERIFIED'
  if grep -q 'Invalid physicalDevice' "$log"; then
    echo 'S14_TURNIP27_XCB_WSI=INVALID_PHYSICAL_DEVICE'
  fi
fi
if ((rc!=0)); then exit "$rc";fi
