#!/data/data/com.termux/files/usr/bin/bash
# S28 Adreno 840 private KGSL Xwayland build only. Never rewrite the
# existing Xwayland running XFCE, global Termux-X11 or Android APK.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source"
BUILD="$ROOT/.source/gpu-stage1/xwayland-kgsl/build"
BASE="$BUILD/hw/xwayland/Xwayland"
EDITED="$SOURCE/hw/xwayland/xwayland-window.c"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0005-kgsl-gpu-window-resize-frame-preserve.patch"
OUT="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve"
BASE_HASH=f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701
SOURCE_BASE_HASH=fc498e577f97f45e244851cfb0ecc7fc9a9d19da06eea718fcdfc1b3ed96533d
SOURCE_PATCH_HASH=ff6d3c65a903cb4db6b755a92393b5a9adf61b431fecd82c23d4ee2bff911b43

[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S28_BUILD=BLOCKED_ONLY_VALIDATED_S26U >&2;exit 2;
}
[[ -s "$PATCH" && -f "$BUILD/build.ninja" && -f "$EDITED" &&
   ! -L "$EDITED" && -x "$BASE" ]] || {
    echo S28_BUILD=BLOCKED_REQUIRES_PREPARED_PRIVATE_KGSL_XWAYLAND >&2;exit 2;
}
[[ "$(sha256sum "$BASE"|cut -d' ' -f1)" == "$BASE_HASH" ]] || {
    echo S28_BUILD=BLOCKED_PRIVATE_XWAYLAND_BASELINE_CHANGED >&2;exit 2;
}
mkdir -p "$OUT/logs" "$OUT/runtime"
system_hash="$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)"
before="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"

case "$(sha256sum "$EDITED"|cut -d' ' -f1)" in
    "$SOURCE_PATCH_HASH") echo S28_SOURCE=ALREADY_PATCHED ;;
    "$SOURCE_BASE_HASH")
        (cd "$SOURCE" && patch --batch --silent --dry-run -p1 < "$PATCH") || {
            echo S28_BUILD=PATCH_MISMATCH >&2;exit 3;
        }
        (cd "$SOURCE" && patch --batch --silent -p1 < "$PATCH")
        echo S28_SOURCE=PATCHED_PRIVATE_ONLY ;;
    *) echo S28_BUILD=BLOCKED_SOURCE_PROVENANCE_MISMATCH >&2;exit 3 ;;
esac

[[ "$(sha256sum "$EDITED"|cut -d' ' -f1)" == "$SOURCE_PATCH_HASH" ]] || {
    echo S28_BUILD=SOURCE_HASH_MISMATCH >&2;exit 3;
}
python3 graphics/tools/relink-s28-private-xwayland.py || {
    tail -26 "$OUT/logs/relink.log" >&2;exit 4;
}
[[ "$(sha256sum "$BASE"|cut -d' ' -f1)" == "$BASE_HASH" &&
   "$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)" == "$system_hash" ]] || {
    echo S28_BUILD=FAIL_BASELINE_OR_INSTALLED_SERVER_CHANGED >&2;exit 5;
}
after="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$before" == "$after" ]] || { echo S28_BUILD=FAIL_ACTIVE_XWAYLAND_CHANGED >&2;exit 5; }
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/s28-protected-drivers.log"
echo S28_KGSL_GPU_COPY_RESIZE_PRESERVE=PRIVATE_BUILT
echo S28_PREVIOUS_S16_S27_XWAYLAND_AND_XFCE=UNCHANGED
echo S28_INSTALLED_TERMUX_X11=UNCHANGED
echo S28_AWSTART_DEFAULT=UNCHANGED
echo S28_PHYSICAL_FLICKER=USER_TEST_REQUIRED
