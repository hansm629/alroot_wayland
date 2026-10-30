#!/data/data/com.termux/files/usr/bin/bash
# S29 source/patch/driver integrity, using the live user session read-only.
# Does NOT restart XFCE, install a binary or touch Termux-X11 :0.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland/xwayland-window.c"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0006-kgsl-native-pixel-resize-preserve.patch"
OLD="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
NEW="$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland"
ORIGINAL="$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"

bash -n termux/alroot-wayland graphics/tools/build-xwayland-private.sh \
    graphics/tools/build-s29-kgsl-native-resize.sh \
    graphics/tools/start-s29-kgsl-native-resize.sh
python3 -m py_compile graphics/tools/relink-s29-private-xwayland.py \
    graphics/tools/test-s29-native-geometry.py \
    graphics/tools/private_kgsl_xwayland_session.py

python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
checks = {
    'strict_optin': 'getenv("S29_KGSL_NATIVE_RESIZE")' in source,
    'qualcomm_kgsl_only': 'xwl_glamor_is_kgsl_surfaceless(xwl_screen)' in source,
    'vulkan_xcb_window_only': 'MakeAtom("_MESA_DRV", 9, FALSE)' in source,
    'not_desktop_root': 'window == window->drawable.pScreen->root' in source,
    'not_rootless': 'xwl_screen->rootless' in source,
    'no_scale_transform': 'pixman_double_to_fixed((double)' not in source,
    'no_bilinear_blur': 'SetPictureFilter(src, "bilinear"' not in source,
    'no_repeated_tiles': 'for (unsigned int yy = 0; yy < height;' not in source,
    'background_pixel_gpu_only': 'CREATE_PIXMAP_USAGE_SCRATCH' in source and
                                 'CPRepeat, &repeat' in source,
    'native_gpu_copy_region': 'CompositePicture(PictOpSrc, src, NULL, dst,' in source,
    'no_cpu_pixel_readback': 'XGetImage(' not in source and 'glReadPixels(' not in source,
    'snapshots_destroyed': 'screen->DestroyPixmap(s28_prior_frame)' in source and
                           'screen->DestroyPixmap(background)' in source,
}
for key, value in checks.items():
    print('S29_SOURCE_' + key.upper() + '=' + ('PASS' if value else 'FAIL'))
    if not value:
        raise SystemExit(3)
PY

[[ -s "$PATCH" && -s "$SOURCE" && -x "$OLD" && -x "$NEW" && -x "$ORIGINAL" ]] || {
    echo S29_CHECK=BLOCKED_MISSING_PRIVATE_SOURCES >&2;exit 2;
}
[[ "$(sha256sum "$ORIGINAL" | cut -d' ' -f1)" == f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701 &&
   "$(sha256sum "$OLD" | cut -d' ' -f1)" == cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7 &&
   "$(sha256sum "$NEW" | cut -d' ' -f1)" == 3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c ]] || {
    echo S29_CHECK=BLOCKED_BINARY_SIGNATURE_CHANGED >&2;exit 2;
}
echo S29_PRIVATE_S16_S28_S29_BINARY_HASHES=PASS

tmp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/s29-patch-verify.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/hw/xwayland"
cp "$SOURCE" "$tmp/hw/xwayland/xwayland-window.c"
(cd "$tmp" && patch -R --batch --silent -p1 < "$PATCH")
[[ "$(sha256sum "$tmp/hw/xwayland/xwayland-window.c"|cut -d' ' -f1)" == ff6d3c65a903cb4db6b755a92393b5a9adf61b431fecd82c23d4ee2bff911b43 ]] || {
    echo S29_CHECK=FAIL_BASELINE_PATCH_REVERSE_HASH >&2;exit 2;
}
(cd "$tmp" && patch --batch --silent -p1 < "$PATCH")
cmp "$tmp/hw/xwayland/xwayland-window.c" "$SOURCE"
echo S29_PATCH_REPLAY_AND_SOURCE_PROVENANCE=PASS

before="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
bash graphics/tools/start-s29-kgsl-native-resize.sh --restart --dry-run \
    > "$tmp/s29-optin-dryrun.log"
bash graphics/tools/start-s29-kgsl-native-resize.sh --restore-s28 --dry-run \
    > "$tmp/s29-restore-dryrun.log"
grep -Fxq 'S29_DRY_RUN_SELECTION=s29' "$tmp/s29-optin-dryrun.log"
grep -Fxq 'S29_DRY_RUN_SELECTION=s28' "$tmp/s29-restore-dryrun.log"
after="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$before" == "$after" ]] || { echo S29_USER_DESKTOP_PID_CHANGED >&2;exit 4; }
echo S29_DRY_RUN_AND_EXISTING_XFCE_UNCHANGED=PASS

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$tmp/protected-drivers.txt"
echo S29_SYSTEM_GPU_AND_TERMUX_X11=UNCHANGED
echo S29_USER_PHYSICAL_ANDROID_VISUAL=NOT_YET_TESTED
