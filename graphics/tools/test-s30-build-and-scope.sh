#!/data/data/com.termux/files/usr/bin/bash
# S30 read-only source and binary integrity checks for private Adreno KGSL.
# No installed driver, active XFCE or Android helper is restarted or changed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
PRIVATE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0007-kgsl-tiny-ahb-stable-xcb-present.patch"
BASE="$ROOT/.source/gpu-stage1/s30-min-extent/base/hw/xwayland"
OUT="$ROOT/.source/gpu-stage1/s30-min-extent"

bash -n termux/alroot-wayland graphics/tools/build-xwayland-private.sh \
    graphics/tools/build-s30-kgsl-stable-resize.sh \
    graphics/tools/start-s30-kgsl-stable-resize.sh
python3 -m py_compile \
    graphics/tools/relink-s30-private-xwayland.py \
    graphics/tools/test-s30-extreme-resize.py \
    graphics/tools/test-s30-native-stable-frame.py \
    graphics/tools/private_kgsl_xwayland_session.py

[[ -s "$PATCH" ]] || { echo S30_SCOPE=BLOCKED_PATCH_MISSING >&2;exit 2; }
python3 - "$PRIVATE" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
gbm = (p / 'xwayland-glamor-gbm.c').read_text()
window = (p / 'xwayland-window.c').read_text()
present = (p / 'xwayland-present.c').read_text()
tests = {
  'S16_AHB_KEPT': 'AHardwareBuffer_recvHandleFromUnixSocket' in gbm,
  'STRICT_TINY_OPTIN': 'S30_KGSL_TINY_AHB_FD0' in gbm,
  'TINY_ONLY_WIDTH_LE_64': 'width >= 1 && width <= 64' in gbm,
  'TINY_EXACTLY_TWO_FDS': 'handle->numFds == 2' in gbm,
  'TINY_MATCHING_SIZE_DISTINCT_INODE': 'img.st_size == meta.st_size && img.st_ino != meta.st_ino' in gbm,
  'NO_GLOBAL_FD_ZERO_FALLBACK': 'if (selected < 0 && s30_tiny' in gbm,
  'ACTUAL_KGSL_DRI3_IMAGE_DETECTION': 'private_pixmap->kgsl_dri3_import' in gbm,
  'S30_STABLE_FRAME_OPTIN': 'getenv("S30_KGSL_STABLE_FRAME")' in window,
  'PRIVATELY_SCOPED_VULKAN_WINDOW': 'MakeAtom("_MESA_DRV", 9, FALSE)' in window,
  'NO_BILINEAR_SCALING': 'SetPictureFilter(src, "bilinear"' not in window,
  'WINDOW_LOCAL_SNAPSHOT': 's30_capture_stable_frame(window, &window->drawable)' in window,
  'NEVER_OVERWRITE_REAL_DRI3': '!xwl_glamor_kgsl_is_dri3_pixmap(pixmap)' in window,
  'LAST_REAL_FRAME_NOT_RESAMPLED': 'if (frame && frame->pixmap)' in window,
  'RELEASE_SNAPSHOT_AFTER_TRUE_PRESENT': 'xwl_window_s30_real_present(window)' in present,
  'PROTECT_S24_S26_CLIENT_DRIVER': 'S30_KGSL_TINY_AHB_FD0' not in present,
  'NO_SERVER_CPU_PIXEL_READBACK': 'XGetImage(' not in window and 'glReadPixels(' not in window,
}
for label, ok in tests.items():
    print('S30_SOURCE_' + label + '=' + ('PASS' if ok else 'FAIL'))
    if not ok:
        raise SystemExit(3)
PY

before="$(awstart --status)"
xwayland_pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
xfce_pid="$(sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
[[ "$xwayland_pid" =~ ^[0-9]+$ && "$xfce_pid" =~ ^[0-9]+$ ]] || {
    echo S30_SCOPE=BLOCKED_NO_RUNNING_PRIVATE_XFCE >&2;exit 3;
}

declare -a files=(xwayland-glamor-gbm.c xwayland-glamor-gbm.h xwayland-window.c xwayland-window.h xwayland-present.c)
declare -a after_sha=(
    1f1bd939d026eaa5e1e695488a63c75dcde5af382a4ff3c9f6d1d2642f0aa487
    09bee5fafc671cfcd7b23f6989a6fc2698d5e00ee604151cf943227917afd949
    7d0e60471652d6fc7a4076f9d18d182d87772d292c541c63fb06f18c23d4e854
    4328577e7e9dc0295c9261c65cc0ef5a2318f56c70a95f1bd2152d4b24627994
    8226eec16be20a1e1dee199cdaa2559e89649f0958e090eabf773171203f0395
)
for i in "${!files[@]}";do
    [[ "$(sha256sum "$PRIVATE/${files[$i]}"|cut -d' ' -f1)" == "${after_sha[$i]}" ]] || {
        echo S30_SCOPE=FAILED_PRIVATE_OVERLAY_CONTENT_CHANGED >&2;exit 3
    }
done

tmp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-s30-patch-replay.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/hw/xwayland"
for file in "${files[@]}";do cp -p "$BASE/$file" "$tmp/hw/xwayland/$file";done
(cd "$tmp" && patch --silent --batch -p1 < "$PATCH")
for file in "${files[@]}";do
    cmp "$tmp/hw/xwayland/$file" "$PRIVATE/$file"
done
echo S30_FIVE_SOURCE_PATCHES_BYTE_REPLAY=PASS

declare -a approved=(
    "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$OUT/runtime/Xwayland|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
)
for item in "${approved[@]}";do
    path="${item%%|*}"
    hash="${item#*|}"
    [[ -x "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "$hash" ]] || {
        echo S30_SCOPE=FAILED_PINNED_PRIVATE_EXECUTABLE_CHANGED >&2;exit 4
    }
done
echo S30_ALL_FOUR_PRIVATE_XWAYLAND_BINARIES_PINNED=PASS

bash graphics/tools/start-s30-kgsl-stable-resize.sh --restart --dry-run \
    > "$tmp/s30-dryrun.log"
bash graphics/tools/start-s30-kgsl-stable-resize.sh --restore-s29 --dry-run \
    > "$tmp/s29-rollback-dryrun.log"
grep -Fxq 'S30_DRY_RUN_MODE=s30' "$tmp/s30-dryrun.log"
grep -Fxq 'S30_DRY_RUN_MODE=s29' "$tmp/s29-rollback-dryrun.log"
echo S30_USER_SESSION_RESTART_AND_ROLLBACK_DRY_RUN=PASS

python3 graphics/tools/test-s28-s26-visual-session-guard.py \
    > "$tmp/approved-binary-session-guard.log"
echo S30_S24_S26_VISUAL_WRAPPER_ALLOWLIST=PASS

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$tmp/protected-system-gpu.sha256"
echo S30_GLOBAL_TERMUX_X11_AND_GPU_DRIVERS=UNCHANGED

after="$(awstart --status)"
grep -Fxq "xwayland: RUNNING $xwayland_pid" <<< "$after"
grep -Fxq "xfce: RUNNING $xfce_pid" <<< "$after"
grep -Fq 'x11_desktop: READY' <<< "$after"
echo S30_CURRENT_XFCE_AND_XWAYLAND_PROCESSES=UNCHANGED
echo S30_PHYSICAL_ANDROID_MANUAL_RESIZE=USER_VISUAL_TEST_REQUIRED
