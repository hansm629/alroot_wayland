#!/data/data/com.termux/files/usr/bin/bash
# Validate S28 private KGSL source/binary scope with NO desktop restart.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
BASE="$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland/xwayland-window.c"
CANDIDATE="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0005-kgsl-gpu-window-resize-frame-preserve.patch"
SNAPSHOT="$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/xwayland-window.s27-base.c"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"

bash -n graphics/tools/build-xwayland-private.sh \
    graphics/tools/build-s28-kgsl-resize-preserve.sh \
    graphics/tools/start-s28-kgsl-resize-preserve.sh termux/alroot-wayland
python3 -m py_compile graphics/tools/relink-s28-private-xwayland.py \
    graphics/tools/test-s28-kgsl-resize-preserve.py

python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
tests = {
    'explicit_s28_optin': 'S28_KGSL_RESIZE_PRESERVE' in source,
    'private_kgsl_only': 'xwl_glamor_is_kgsl_surfaceless(xwl_screen)' in source,
    'exclude_rootless': 'xwl_screen->rootless' in source,
    'exclude_desktop_root': 'window == window->drawable.pScreen->root' in source,
    'vulkan_xcb_only': 'MakeAtom("_MESA_DRV", 9, FALSE)' in source,
    'x11_old_frame_gpu_snapshot': 's28_prior_frame = (*screen->CreatePixmap)' in source,
    'gpu_xrender_scaled_preservation': 'CompositePicture(PictOpSrc, src, NULL, dst,' in source,
    'no_cpu_pixel_map_or_readback': 'XGetImage(' not in source and
                                     'glReadPixels(' not in source,
    'reference_cleanup': 'screen->DestroyPixmap(s28_prior_frame)' in source,
    'fallback_gpu_copyarea': '(*gc->ops->CopyArea)' in source,
}
for name, ok in tests.items():
    print('S28_SOURCE_'+name.upper()+'='+('PASS' if ok else 'FAIL'))
    if not ok:
        raise SystemExit(3)
PY

[[ -s "$PATCH" && -s "$SNAPSHOT" && -x "$BASE" && -x "$CANDIDATE" ]] || {
    echo S28_SOURCE_PINNED_INPUT_MISSING=FAIL >&2;exit 2;
}
temp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/s28-patch-replay.XXXXXX")"
trap 'rm -rf "$temp"' EXIT
mkdir -p "$temp/hw/xwayland"
cp "$SNAPSHOT" "$temp/hw/xwayland/xwayland-window.c"
(cd "$temp" && patch --silent --batch -p1 < "$PATCH")
cmp "$temp/hw/xwayland/xwayland-window.c" "$SOURCE"
echo S28_SOURCE_PATCH_BYTE_REPLAY=PASS

[[ "$(sha256sum "$BASE"|cut -d' ' -f1)" == f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701 &&
   "$(sha256sum "$CANDIDATE"|cut -d' ' -f1)" == cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7 ]] || {
    echo S28_BINARY_PROVENANCE=FAIL >&2;exit 2;
}
echo S28_ORIGINAL_PRIVATE_XWAYLAND_UNCHANGED=PASS
echo S28_SEPARATE_CANDIDATE_BINARY=PASS

pid_before="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
bash graphics/tools/start-s28-kgsl-resize-preserve.sh --restart --dry-run \
    > "$temp/s28-optin-dryrun.txt"
bash graphics/tools/start-s28-kgsl-resize-preserve.sh --restore --dry-run \
    > "$temp/s28-restore-dryrun.txt"
grep -Fxq 'S28_DRY_RUN_MODE=s28' "$temp/s28-optin-dryrun.txt"
grep -Fxq 'S28_DRY_RUN_MODE=restore' "$temp/s28-restore-dryrun.txt"
pid_after="$(awstart --status|sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p')"
[[ "$pid_before" == "$pid_after" ]] || {
    echo S28_RUNNING_XWAYLAND_PID_CHANGED=FAIL >&2;exit 3;
}
echo S28_AWSTART_DRY_RUN_NO_DESKTOP_MUTATION=PASS

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$temp/system-hashes.log"
echo S28_INSTALLED_GPU_AND_TERMUX_X11=UNCHANGED
echo S28_USER_VISUAL_FLICKER=TEST_PENDING
