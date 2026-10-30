#!/data/data/com.termux/files/usr/bin/bash
# S26 opt-in XCB DRI3 AHB transient retry candidate, built only inside the
# project's existing private S24 wrapper-Meson overlay. Do not install it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
REF="${ALROOT_WRAPPER_SOURCE:-$HOME/alroot-vk-lab/vulkan-wrapper-android}"
WORK="$ROOT/.source/gpu-stage1/wrapper-meson"
SRC="$WORK/source"
BUILD="$WORK/build"
OUT="$ROOT/.source/gpu-stage1/s26-wrapper-resize-retry"
PATCH="$ROOT/graphics/patches/mesa/wrapper/0004-s26-xcb-dri3-transient-retry.patch"
PRIVATE="$SRC/src/vulkan/wsi/wsi_common_x11.c"
PIN=b3b32abb82af4d374fd35c7192a7f524a5fc8bd3
cd "$ROOT"
[[ -s "$PATCH" && -f "$BUILD/build.ninja" && -f "$PRIVATE" && ! -L "$PRIVATE" ]] || {
    echo S26_BUILD=BLOCKED_REQUIRES_PRIVATE_S24_OVERLAY >&2;exit 2;
}
[[ "$(git -C "$REF" rev-parse HEAD)" == "$PIN" &&
   -z "$(git -C "$REF" status --porcelain)" ]] || {
    echo S26_BUILD=BLOCKED_REFERENCE_MODIFIED >&2;exit 2;
}
rg -q 'S24_CREATE_VIEW_REJECT_STALE' "$SRC/src/vulkan/wrapper/wrapper_device.c"
rg -q 'uint16_t dri3_height = pCreateInfo->imageExtent.height' "$PRIVATE"
rg -q 'S16_WRAPPER_X11_WSI_AHB_SEND_HANDLE_RC' "$PRIVATE"
mkdir -p "$OUT/runtime" "$OUT/logs"
system_before="$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)"
s24="$ROOT/.source/gpu-stage1/s24-wrapper-xcb-resize/runtime/libvulkan_wrapper.so"
[[ -s "$s24" ]] || { echo S26_BUILD=BLOCKED_S24_RUNTIME_MISSING >&2;exit 2; }
s24_before="$(sha256sum "$s24" | cut -d' ' -f1)"

if (cd "$SRC" && patch --batch --silent --dry-run -R -p1 < "$PATCH") 2>/dev/null; then
    echo S26_PRIVATE_SOURCE=ALREADY_APPLIED
else
    (cd "$SRC" && patch --batch --dry-run -p1 < "$PATCH") || {
        echo S26_BUILD=BLOCKED_UNEXPECTED_PRIVATE_SOURCE >&2;exit 3;
    }
    (cd "$SRC" && patch --batch --forward -p1 < "$PATCH")
    echo S26_PRIVATE_SOURCE=PATCHED
fi

ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-2}" \
    src/vulkan/wrapper/libvulkan_wrapper.so > "$OUT/logs/build.log" 2>&1 || {
    echo S26_BUILD=FAILED >&2
    tail -30 "$OUT/logs/build.log" >&2
    exit 4
}
target="$OUT/runtime/libvulkan_wrapper.so"
cp -p "$BUILD/src/vulkan/wrapper/libvulkan_wrapper.so" "$target.tmp"
mv -f "$target.tmp" "$target"
python3 - "$OUT/runtime" <<'PY'
import json, pathlib, sys
p = pathlib.Path(sys.argv[1]).resolve()
d = {'file_format_version': '1.0.1',
     'ICD': {'library_path': str(p / 'libvulkan_wrapper.so'),
             'api_version': '1.3.359'}}
(p / 'wrapper_icd.private.json').write_text(json.dumps(d, indent=2) + '\n')
PY

[[ "$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)" == "$system_before" &&
   "$(sha256sum "$s24" | cut -d' ' -f1)" == "$s24_before" &&
   -z "$(git -C "$REF" status --porcelain)" ]] || {
    echo S26_BUILD=FAIL_UNEXPECTED_REFERENCE_OR_SYSTEM_CHANGE >&2;exit 5;
}
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/protected-drivers.sha256.log"

echo S26_PRIVATE_VULKAN_WRAPPER=BUILT
echo "S26_ICD_PATH=$OUT/runtime/wrapper_icd.private.json"
echo S26_TRANSIENT_AHB_DRI3_RETRY=OPT_IN_ONLY
echo S26_TEST_INJECTION=OFF_UNLESS_EXPLICITLY_SET
echo S26_SYSTEM_DRIVER_AND_S24_BASELINE=UNCHANGED
echo S26_AWSTART_DEFAULT=UNCHANGED
