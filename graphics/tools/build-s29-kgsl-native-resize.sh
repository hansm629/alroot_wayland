#!/data/data/com.termux/files/usr/bin/bash
# S29 pixel-exact GPU resize. Build privately, preserve all installed and
# currently running Xwayland binaries, no implicit desktop restart.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source"
BUILD="$ROOT/.source/gpu-stage1/xwayland-kgsl/build"
EDITED="$SOURCE/hw/xwayland/xwayland-window.c"
S16="$BUILD/hw/xwayland/Xwayland"
S28="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0006-kgsl-native-pixel-resize-preserve.patch"
OUT="$ROOT/.source/gpu-stage1/s29-native-resize"
BASE_SOURCE_HASH=ff6d3c65a903cb4db6b755a92393b5a9adf61b431fecd82c23d4ee2bff911b43
PATCHED_SOURCE_HASH=d1a3ce07ef84d07b2f06206151a9001d09a69e1adf403d4ab46cd20a22fe31cd
S16_HASH=f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701
S28_HASH=cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7
S29_HASH=3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c

[[ "$(/system/bin/getprop ro.product.model)" == SM-S948* ]] || {
    echo S29_BUILD=BLOCKED_S26_ULTRA_ONLY >&2;exit 2;
}
[[ -s "$PATCH" && -f "$BUILD/build.ninja" && -f "$EDITED" &&
   ! -L "$EDITED" && -x "$S16" && -x "$S28" ]] || {
    echo S29_BUILD=BLOCKED_MISSING_PINNED_PRIVATE_SOURCES >&2;exit 2;
}
[[ "$(sha256sum "$S16"|cut -d' ' -f1)" == "$S16_HASH" &&
   "$(sha256sum "$S28"|cut -d' ' -f1)" == "$S28_HASH" ]] || {
    echo S29_BUILD=BLOCKED_S16_OR_S28_BASELINE_CHANGED >&2;exit 2;
}

mkdir -p "$OUT/logs" "$OUT/runtime"
system_hash="$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)"
oldpid="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
case "$(sha256sum "$EDITED"|cut -d' ' -f1)" in
    "$PATCHED_SOURCE_HASH") echo S29_SOURCE=ALREADY_PATCHED ;;
    "$BASE_SOURCE_HASH")
        (cd "$SOURCE" && patch --batch --silent --dry-run -p1 < "$PATCH") || {
            echo S29_BUILD=PATCH_FAILED_DRY_RUN >&2;exit 3;
        }
        (cd "$SOURCE" && patch --batch --silent -p1 < "$PATCH")
        echo S29_SOURCE=PATCHED_OPT_IN_ONLY ;;
    *) echo S29_BUILD=BLOCKED_PRIVATE_SOURCE_PROVENANCE_MISMATCH >&2;exit 3 ;;
esac

[[ "$(sha256sum "$EDITED"|cut -d' ' -f1)" == "$PATCHED_SOURCE_HASH" ]] || {
    echo S29_BUILD=UNEXPECTED_PRIVATE_SOURCE_HASH >&2;exit 3;
}
python3 graphics/tools/relink-s29-private-xwayland.py || {
    tail -35 "$OUT/logs/relink.log" >&2;exit 4;
}

[[ "$(sha256sum "$OUT/runtime/Xwayland"|cut -d' ' -f1)" == "$S29_HASH" &&
   "$(sha256sum "$S16"|cut -d' ' -f1)" == "$S16_HASH" &&
   "$(sha256sum "$S28"|cut -d' ' -f1)" == "$S28_HASH" &&
   "$(sha256sum "$PREFIX/bin/Xwayland"|cut -d' ' -f1)" == "$system_hash" ]] || {
    echo S29_BUILD=BLOCKED_UNEXPECTED_BINARY_HASH >&2;exit 5;
}
newpid="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$oldpid" == "$newpid" ]] || { echo S29_BUILD=RUNNING_USER_XWAYLAND_CHANGED >&2;exit 5; }
sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$OUT/logs/protected-gpu-drivers.txt"
echo S29_NATIVE_GPU_RESIZE=PRIVATE_BUILD_PASS
echo S29_PREVIOUS_S16_S28_AND_TERMUX_X11=UNCHANGED
echo S29_ACTIVE_USER_XFCE=UNCHANGED
echo S29_AWSTART_DEFAULT=UNCHANGED
echo S29_PHYSICAL_ANDROID_VISUAL=USER_TEST_REQUIRED
