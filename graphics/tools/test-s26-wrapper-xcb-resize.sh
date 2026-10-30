#!/data/data/com.termux/files/usr/bin/bash
# S26 Qualcomm Wrapper XCB/DRI3 resize fault-injection regression.
# Never restarts XFCE, replaces installed drivers, or promotes awstart.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage1/s26-wrapper-resize-retry"
PRIVATE="$ROOT/.source/gpu-stage1/wrapper-meson/source/src/vulkan/wsi/wsi_common_x11.c"
ICD="$OUT/runtime/wrapper_icd.private.json"
S24="$ROOT/.source/gpu-stage1/s24-wrapper-xcb-resize/runtime/libvulkan_wrapper.so"
if [[ "${1:-}" == --visual ]];then
    shift
    echo S26_VISUAL=PRIVATE_AHB_DRI3_RETRY_NOT_AWSTART_DEFAULT
    exec python3 -u graphics/tools/run-s24-wrapper-visual.py "$@" --s26
fi
[[ -s "$ICD" && -s "$OUT/runtime/libvulkan_wrapper.so" &&
   -s "$S24" && -f "$PRIVATE" ]] || {
    echo S26_TEST=BLOCKED_BUILD_PRIVATE_WRAPPER_FIRST >&2;exit 2;
}
mkdir -p "$OUT/logs"
sys_before="$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)"
s24_before="$(sha256sum "$S24" | cut -d' ' -f1)"
before="$(awstart --status | sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$before" =~ ^[0-9]+$ ]] || { echo S26_TEST=BLOCKED_NO_XWAYLAND;exit 2; }
python3 - "$PRIVATE" <<'PY'
from pathlib import Path
import sys
source=Path(sys.argv[1]).read_text()
checks={
    'xcb_badalloc': 'error->error_code == XCB_ALLOC' in source,
    'xcb_badmatch': 'error->error_code == XCB_MATCH' in source,
    'only_ahb': 'image->base.ahardware_buffer' in source,
    'window_liveness': 'if (!still_window)' in source,
    'fresh_image': 'memset(&chain->images[image], 0, sizeof(chain->images[image]))' in source,
    'bounded_retry': 's26_attempt < 2' in source,
    'exact_optin': 's26_exact_optin("S26_WRAPPER_XCB_RETRY_TRANSIENT")' in source,
    'fault_inject': 'S26_XCB_DRI3_INJECTED_TRANSIENT_BADALLOC_ONCE=' in source,
}
for name, ok in checks.items():
    print('S26_SOURCE_'+name.upper()+'='+('PASS' if ok else 'FAIL'))
    if not ok:raise SystemExit(3)
PY

# The negative case must produce the exact user GLX error when we force one
# post-startup DRI3 pixmap failure. The positive case must survive it using
# the private S26 retry. A third test probes the same path under vkcube.
run() {
    local label="$1";shift
    echo "S26[$label]=STARTED"
    timeout -s TERM -k 3s 28s python3 -u \
        graphics/tools/test-s26-xcb-fault-recovery.py "$@" \
        2>&1 | tee "$OUT/logs/$label.log"
    grep -Eq '^S26_XCB_TRANSIENT_IMPORT_RECOVERY=PASS(_EXPECTED_NEGATIVE)?$' \
        "$OUT/logs/$label.log"
    echo "S26[$label]=PASS"
}
run badalloc-no-retry-control --demo zink --fault-once 10 \
    --no-retry --expect-failure --seconds 6
run badalloc-recovered-zink --demo zink --fault-once 10 --seconds 6
run badalloc-recovered-vulkan --demo vulkan --fault-once 10 --seconds 6

if [[ "${S26_RESIZE_STRESS:-0}" == 1 ]];then
    echo S26_CONTINUOUS_XCB_RESIZE=STRESS
    timeout 55s python3 -u graphics/tools/test-s23-live-xfce-windowed.py \
        --route wrapper-s26 --only-gears --resize-own-window \
        --resize-sizes '900x649,900x650,900x651,901x649,1200x701,777x673,500x500' \
        2>&1 | tee "$OUT/logs/s26-zink-live-stress.log"
    grep -Fxq 'S23_LIVE_MESA27_WINDOWED_MATRIX=PASS' \
        "$OUT/logs/s26-zink-live-stress.log"
    timeout 55s python3 -u graphics/tools/test-s23-live-xfce-windowed.py \
        --route wrapper-s26 --only-vulkan --resize-own-window --edge-check \
        --resize-sizes '901x649,900x650,1437x811,1200x701,500x500' \
        2>&1 | tee "$OUT/logs/s26-vkcube-live-stress.log"
    grep -Fxq 'S23_LIVE_MESA27_WINDOWED_MATRIX=PASS' \
        "$OUT/logs/s26-vkcube-live-stress.log"
fi

after="$(awstart --status | sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$before" == "$after" ]] || { echo S26_EXISTING_XWAYLAND=CHANGED;exit 6; }
[[ "$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)" == "$sys_before" &&
   "$(sha256sum "$S24" | cut -d' ' -f1)" == "$s24_before" ]] || {
    echo S26_SYSTEM_OR_S24_BINARY=CHANGED;exit 7;
}
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/protected-drivers-test.sha256.log"
echo S26_REPRODUCED_AND_RECOVERED_GLX_BAD_CURRENT_WINDOW=PASS
echo S26_S24_BASELINE_AND_SYSTEM_DRIVERS=UNCHANGED
echo S26_EXISTING_XWAYLAND_AND_XFCE=UNCHANGED
echo S26_ANDROID_PHYSICAL_MANUAL_RESIZE=USER_VISUAL_TEST_REQUIRED
echo S26_AWSTART_DEFAULT=NOT_PROMOTED
