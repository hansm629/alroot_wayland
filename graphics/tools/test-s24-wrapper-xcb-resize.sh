#!/data/data/com.termux/files/usr/bin/bash
# Private Android Qualcomm Wrapper + Mesa27 Zink/XCB live-window regression.
# Only changes the size of this script's own bounded vkcube/glxgears windows.
# No profile switches, no XFCE restart, no Termux system driver replacement.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage1/s24-wrapper-xcb-resize"
ICD="$OUT/runtime/wrapper_icd.private.json"
SOURCE="$ROOT/.source/gpu-stage1/wrapper-meson/source/src/vulkan"
LOGS="$OUT/logs"
mkdir -p "$LOGS"
[[ -s "$ICD" && -s "$OUT/runtime/libvulkan_wrapper.so" ]] || {
    echo S24_WRAPPER_TEST=BLOCKED_BUILD_PRIVATE_WRAPPER_FIRST >&2; exit 2;
}

# Verify both logical and physical AHB dimensions are used by separate
# Vulkan images, rather than binding one mismatched image to taller memory.
python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1])
c=(root/'wsi/wsi_common.c').read_text()
a=(root/'wsi/wsi_common_android.c').read_text()
x=(root/'wsi/wsi_common_x11.c').read_text()
g=(root/'wrapper/wrapper_device.c').read_text()
checks={
  'xcb_scoped_blit': 's24_surface->platform == VK_ICD_WSI_PLATFORM_XCB' in c,
  'optin_exact_1': "s24_xcb_pad[0] == '1' && s24_xcb_pad[1] == '\\0'" in c,
  'transfer_image_separate': '.height = info->ahardware_buffer_desc->height' in a,
  'logical_copy_extent': '.extent = info->create.extent' in c,
  'guest_window_logical_size': 'uint16_t dri3_height = pCreateInfo->imageExtent.height;' in x,
  'physical_dri3_extent': 'dri3_height = (uint16_t)physical.height' in x,
  'stale_image_rejection': 'S24_CREATE_VIEW_REJECT_STALE' in g,
  'only_ahb_import_failure': 'return result; /* Never report success with an unbound imported AHB. */' in a,
}
for name, ok in checks.items():
    print('S24_SOURCE_'+name.upper()+'='+('PASS' if ok else 'FAIL'))
    if not ok: raise SystemExit(3)
PY

# A deliberately destroyed VkImage is a *private driver-only* diagnostic.
# It must yield an error without dereferencing the Qualcomm vendor image.
clang --target=aarch64-linux-android35 -std=gnu11 -O2 \
    -Wall -Wextra -Werror \
    graphics/tests/s24-wrapper-stale-imageview.c -lvulkan \
    -o "$OUT/stale-imageview-guard"
env VK_DRIVER_FILES="$ICD" VK_ICD_FILENAMES="$ICD" \
    S24_TRACE_WRAPPER_RESIZE=1 \
    timeout -s TERM -k 3s 16s "$OUT/stale-imageview-guard" \
    > "$LOGS/stale-imageview.log" 2>&1 || {
    echo S24_STALE_IMAGEVIEW=FAIL_OR_CRASH
    tail -20 "$LOGS/stale-imageview.log"
    exit 3
}
grep -Fxq 'S24_STALE_IMAGE_VIEW_GUARD=PASS_CLEAN_ERROR_NO_VENDOR_CALL' \
    "$LOGS/stale-imageview.log"
echo S24_STALE_IMAGEVIEW_VENDOR_CRASH_GUARD=PASS

before="$(awstart --status | sed -n 's/^xwayland: RUNNING \([0-9]*\).*/\1/p')"
[[ "$before" =~ ^[0-9]+$ ]] || { echo S24_LIVE_XFCE=NOT_RUNNING;exit 2; }
installed_hash="$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)"

run() {
    local label="$1" seconds="$2"; shift 2
    local file="$LOGS/$label.log"
    if ! timeout -s TERM -k 3s "$seconds" env S24_WRAPPER_XCB_AHB_PAD_HEIGHT=1 \
        python3 -u graphics/tools/test-s23-live-xfce-windowed.py \
          --route wrapper-s24 "$@" > "$file" 2>&1; then
        echo "S24[$label]=FAIL_OR_TIMEOUT"
        tail -30 "$file"
        return 4
    fi
    grep -Fxq 'S23_LIVE_MESA27_WINDOWED_MATRIX=PASS' "$file" || {
        echo "S24[$label]=FAIL_NO_ACCEPTANCE";return 5;
    }
    echo "S24[$label]=PASS"
}

run vkcube-regular-resize 36 --only-vulkan --resize-own-window --edge-check
run glxgears-regular-resize 42 --only-gears --resize-own-window

# Ordinary aligned swapchain allocation stays on the old direct AHB path
# with the S24 feature absent. No resize of this negative-control window.
for scene in --only-vulkan --only-gears; do
    name="${scene#--only-}"
    file="$LOGS/optin-disabled-$name.log"
    timeout -s TERM -k 3s 24s env -u S24_WRAPPER_XCB_AHB_PAD_HEIGHT \
        python3 -u graphics/tools/test-s23-live-xfce-windowed.py \
          --route wrapper-s24 "$scene" > "$file" 2>&1 || {
        echo "S24[optin-disabled-$name]=FAIL"
        tail -18 "$file"
        exit 4
    }
    grep -Fxq 'S23_LIVE_MESA27_WINDOWED_MATRIX=PASS' "$file"
    echo "S24[optin-disabled-$name]=PASS_UNCHANGED"
done

if [[ "${S24_RESIZE_STRESS:-0}" == 1 ]]; then
    run vkcube-odd-heights 63 --only-vulkan --resize-own-window --edge-check \
      --resize-sizes '900x649,900x650,900x651,901x649,1023x653,1200x701,1437x811,500x500'
    run glxgears-odd-heights 50 --only-gears --resize-own-window \
      --resize-sizes '901x649,1200x701,777x673,500x500'
fi

after="$(awstart --status | sed -n 's/^xwayland: RUNNING \([0-9]*\).*/\1/p')"
[[ "$before" == "$after" ]] || { echo S24_ORIGINAL_XFCE_PID=CHANGED;exit 6; }
[[ "$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)" == "$installed_hash" ]] || {
    echo S24_SYSTEM_WRAPPER_MODIFIED=FAIL;exit 7;
}
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$LOGS/system-driver-sha256.log" || exit 7
echo S24_XCB_AHB_GPU_COPY_REAL_WINDOW_RESIZE=PASS
echo S24_SYSTEM_WRAPPER_TERMUX_X11_APK=UNCHANGED
echo S24_EXISTING_XFCE_SERVER_PID=UNCHANGED
echo S24_ANDROID_PHYSICAL_DISPLAY_PIXELS=USER_VISUAL_TEST_REQUIRED
echo S24_PRODUCTION_AWSTART_DEFAULT=NOT_PROMOTED
