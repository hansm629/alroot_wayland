#!/data/data/com.termux/files/usr/bin/bash
# Rebuild S24 spec-compliant XCB padded-AHB GPU-copy only in the project-private
# Mesa wrapper overlay. Never replaces installed Vulkan Wrapper, ICD JSON,
# original Termux-X11, Alroot APK, or a running XFCE desktop.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
REF="${ALROOT_WRAPPER_SOURCE:-$HOME/alroot-vk-lab/vulkan-wrapper-android}"
WORK="$ROOT/.source/gpu-stage1/wrapper-meson"
SRC="$WORK/source"
BUILD="$WORK/build"
OUT="$ROOT/.source/gpu-stage1/s24-wrapper-xcb-resize"
PATCH="$ROOT/graphics/patches/mesa/wrapper/0003-s24-xcb-ahb-aligned-gpu-copy.patch"
PIN=b3b32abb82af4d374fd35c7192a7f524a5fc8bd3
cd "$ROOT"

[[ -s "$PATCH" && -f "$BUILD/build.ninja" && -d "$SRC/src/vulkan/wsi" ]] || {
    echo S24_WRAPPER=BLOCKED_REQUIRES_PREPARED_PRIVATE_S16_WSI_OVERLAY >&2;exit 2;
}
[[ "$(git -C "$REF" rev-parse HEAD)" == "$PIN" &&
   -z "$(git -C "$REF" status --porcelain)" ]] || {
    echo S24_WRAPPER=BLOCKED_PINNED_REFERENCE_NOT_CLEAN >&2;exit 2;
}
[[ -f "$SRC/src/vulkan/wsi/wsi_common.c" &&
   -f "$SRC/src/vulkan/wsi/wsi_common_x11.c" ]] || exit 2
rg -q 'WSI_IMAGE_TYPE_ANDROID' "$SRC/src/vulkan/wsi/wsi_common.c"
rg -q 'S16_WRAPPER_X11_WSI_AHB_SEND_HANDLE_RC' \
    "$SRC/src/vulkan/wsi/wsi_common_x11.c" || {
    echo S24_WRAPPER=BLOCKED_PRIVATE_AHB_XCB_SOCKET_WSI_NOT_PREPARED >&2;exit 2;
}

mkdir -p "$OUT/runtime" "$OUT/logs"
installed_before="$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)"

# The S16 overlay may still contain symlinks into the user's immutable Mesa
# reference. Never let the patch utility follow a symlink into those files.
for relative in \
    src/vulkan/wsi/wsi_common.c \
    src/vulkan/wsi/wsi_common_android.c \
    src/vulkan/wsi/wsi_common_x11.c \
    src/vulkan/wrapper/wrapper_device.c; do
    target="$SRC/$relative"
    [[ -f "$target" ]] || { echo "S24_WRAPPER=BLOCKED_MISSING:$relative" >&2;exit 2; }
    if [[ -L "$target" ]]; then
        [[ "$(realpath "$target")" == "$(realpath "$REF/$relative")" ]] || {
            echo "S24_WRAPPER=BLOCKED_UNTRUSTED_SYMLINK:$relative" >&2;exit 2;
        }
        rm "$target"
        cp "$REF/$relative" "$target"
    fi
done

if (cd "$SRC" && patch --batch --silent --dry-run -R -p1 < "$PATCH") 2>/dev/null; then
    echo S24_PRIVATE_WRAPPER_SOURCES=ALREADY_APPLIED
else
    (cd "$SRC" && patch --batch --silent --dry-run -p1 < "$PATCH") || {
        echo S24_WRAPPER=BLOCKED_PATCH_BASELINE_MISMATCH >&2;exit 3;
    }
    (cd "$SRC" && patch --batch --forward -p1 < "$PATCH")
    echo S24_PRIVATE_WRAPPER_SOURCES=PATCHED
fi

ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-2}" \
    src/vulkan/wrapper/libvulkan_wrapper.so > "$OUT/logs/repro-build.log" 2>&1 || {
    echo S24_WRAPPER_BUILD=FAIL >&2
    tail -32 "$OUT/logs/repro-build.log" >&2
    exit 4
}
binary="$OUT/runtime/libvulkan_wrapper.so"
staged="$OUT/runtime/.libvulkan_wrapper.so.staging"
cp -p "$BUILD/src/vulkan/wrapper/libvulkan_wrapper.so" "$staged"
mv -f "$staged" "$binary"
python3 - "$OUT/runtime" <<'PY'
import json
import pathlib
import sys
target=pathlib.Path(sys.argv[1]).resolve()
manifest={'file_format_version':'1.0.1',
          'ICD':{'library_path':str(target/'libvulkan_wrapper.so'),
                 'api_version':'1.3.359'}}
(target/'wrapper_icd.private.json').write_text(json.dumps(manifest,indent=2)+'\n')
PY

[[ "$(sha256sum "$PREFIX/lib/libvulkan_wrapper.so" | cut -d' ' -f1)" == "$installed_before" ]] || {
    echo S24_INSTALLED_WRAPPER_HASH=FAIL >&2;exit 5;
}
[[ -z "$(git -C "$REF" status --porcelain)" ]] || exit 5
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/system-graphics-unchanged.log" || exit 5

echo S24_PRIVATE_WRAPPER_ICD=BUILT_AND_STAGED
echo "S24_PRIVATE_WRAPPER_ICD_PATH=$OUT/runtime/wrapper_icd.private.json"
echo S24_XCB_AHB_PADDED_GPU_COPY=OPT_IN_ONLY_EXACT_ENV_VALUE_1
echo S24_INSTALLED_WRAPPER_AND_TERMUX_X11=UNCHANGED
echo S24_AWSTART_DEFAULT=UNCHANGED
echo S24_REAL_XCB_PIXELS=REQUIRES_LIVE_TEST
