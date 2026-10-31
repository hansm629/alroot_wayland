#!/data/data/com.termux/files/usr/bin/bash
# S31 strict source/binary/launch scope checks. Read-only on active XFCE.
# Do not restart the desktop or update any system GPU drivers / Termux-X11.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0008-kgsl-adaptive-resize-present-extent.patch"
OUT="$ROOT/.source/gpu-stage1/s31-resize-continuity"

bash -n termux/alroot-wayland graphics/tools/build-xwayland-private.sh \
    graphics/tools/build-s31-kgsl-resize-continuity.sh \
    graphics/tools/start-s31-kgsl-resize-continuity.sh
python3 -m py_compile \
    graphics/tools/private_kgsl_xwayland_session.py \
    graphics/tools/relink-s31-private-xwayland.py \
    graphics/tools/test-s31-resize-continuity.py

python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys
r = Path(sys.argv[1])
window = (r/'xwayland-window.c').read_text()
present = (r/'xwayland-present.c').read_text()
checks = {
    'EXACT_S31_MODE3': 'S31_KGSL_RESIZE_CONTENT_POLICY' in window and
                       "s31[0] == '3'" in window,
    'ROOTLESS_AND_VENDOR_GATED': 'xwl_screen->rootless' in window and
                                  'xwl_glamor_is_kgsl_surfaceless(xwl_screen)' in window,
    'VULKAN_GLX_MESA_WINDOW_TAG': 'MakeAtom("_MESA_DRV", 9, FALSE)' in window,
    'NO_STALE_GPU_FRAME_STRETCH': 'SetPictureFilter(src, "bilinear"' not in window,
    'ADAPTIVE_PREVIOUS_FRAME_BOUNDED': 'abs(sw - dw) <=' in window,
    'STALE_BUFFER_GATED_ON_REAL_PRESENT': 'stale_extent = abs(sw - w)' in window,
    'UNMATCHED_CROP_CLEARED': 'if (stale_extent)' in window and
                               '0, 0, w, h' in window,
    'MATCHED_RENDERED_CONTENT_PRESERVED': 'if (sw < w)' in window and
                                          'if (sh < h)' in window,
    'NO_IMPORT_SOURCE_PIXMAP_WRITE': '!xwl_glamor_kgsl_is_dri3_pixmap(pixmap)' in window,
    'ONLY_CLEAR_AFTER_REAL_PRESENT_COPY': 'present_execute_copy(vblank, crtc_msc);' in present and
                                          'xwl_window_s31_fill_uncovered_present(window, vblank->pixmap);' in present,
    'S30_TINY_AHB_RECOVERY_KEPT': 'S30_KGSL_TINY_AHB_FD0' in (r/'xwayland-glamor-gbm.c').read_text(),
    'NO_NEW_SERVER_CPU_READBACK': 'XGetImage(' not in window and
                                  'glReadPixels(' not in window,
}
for name, passed in checks.items():
    print('S31_SCOPE_'+name+'='+('PASS' if passed else 'FAIL'))
    if not passed:
        raise SystemExit(3)
PY

declare -a names=(xwayland-window.c xwayland-window.h xwayland-present.c)
declare -a source_hashes=(
    9d422b27e55d9848f3e8341402a9bb966d7a448d2ce5e38b8cf8e61ba466ac77
    2e0751cd815130255f72fb0fc709370ef38b6ac8f6db477f2ac131bfda844809
    49195bcf51e8a9fff9f0ef17e85e9e459518b982bd1ee9ea1984162da20a3575
)
for i in "${!names[@]}";do
    path="$SOURCE/${names[$i]}"
    [[ -f "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "${source_hashes[$i]}" ]] || {
        echo S31_SCOPE=FAIL_UNEXPECTED_PRIVATE_SOURCE >&2;exit 3;
    }
done
echo S31_PRIVATE_SOURCE_HASHES=PASS

[[ -s "$PATCH" ]] || { echo S31_SCOPE=FAIL_MISSING_PATCH >&2;exit 2; }
temp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/s31-source-replay.XXXXXX")"
trap 'rm -rf "$temp"' EXIT
mkdir -p "$temp/hw/xwayland"
cp "$SOURCE/xwayland-window.c" "$temp/hw/xwayland/xwayland-window.c"
cp "$SOURCE/xwayland-window.h" "$temp/hw/xwayland/xwayland-window.h"
cp "$SOURCE/xwayland-present.c" "$temp/hw/xwayland/xwayland-present.c"
(cd "$temp" && patch --batch --silent -R -p1 < "$PATCH")
for spec in \
    'xwayland-window.c|7d0e60471652d6fc7a4076f9d18d182d87772d292c541c63fb06f18c23d4e854' \
    'xwayland-window.h|4328577e7e9dc0295c9261c65cc0ef5a2318f56c70a95f1bd2152d4b24627994' \
    'xwayland-present.c|8226eec16be20a1e1dee199cdaa2559e89649f0958e090eabf773171203f0395';do
    file="${spec%%|*}";expected="${spec#*|}"
    [[ "$(sha256sum "$temp/hw/xwayland/$file"|cut -d' ' -f1)" == "$expected" ]] || {
        echo S31_SCOPE=FAILED_S30_BASELINE_REVERSE_PATCH >&2;exit 3;
    }
done
(cd "$temp" && patch --batch --silent -p1 < "$PATCH")
for file in "${names[@]}";do
    cmp "$temp/hw/xwayland/$file" "$SOURCE/$file"
done
echo S31_PATCH_REPLAY_BYTE_EXACT=PASS

declare -a approved=(
    "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$OUT/runtime/Xwayland|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
)
for spec in "${approved[@]}";do
    f="${spec%%|*}";expected="${spec#*|}"
    [[ -x "$f" && ! -L "$f" &&
       "$(sha256sum "$f"|cut -d' ' -f1)" == "$expected" ]] || {
        echo S31_SCOPE=FAILED_PINNED_PRIVATE_XWAYLAND_HASH >&2;exit 4;
    }
done
echo S31_ALL_FIVE_PRIVATE_XWAYLAND_BINARIES_PINNED=PASS

before="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
xfce="$(sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
[[ "$pid" =~ ^[0-9]+$ && "$xfce" =~ ^[0-9]+$ ]] || exit 4
bash graphics/tools/start-s31-kgsl-resize-continuity.sh --restart --dry-run \
    > "$temp/s31-preview.log"
bash graphics/tools/start-s31-kgsl-resize-continuity.sh --restore-s30 --dry-run \
    > "$temp/s30-rollback.log"
grep -Fxq 'S31_DRY_RUN_MODE=s31' "$temp/s31-preview.log"
grep -Fxq 'S31_DRY_RUN_MODE=s30' "$temp/s30-rollback.log"
python3 graphics/tools/test-s28-s26-visual-session-guard.py \
    > "$temp/visual-test-guard.log"
echo S31_SAFE_SWITCH_AND_S26_VISUAL_GUARD=PASS

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$temp/installed-drivers.log"
echo S31_INSTALLED_TERMUX_X11_AND_GPU_DRIVERS=UNCHANGED
after="$(awstart --status)"
grep -Fxq "xwayland: RUNNING $pid" <<< "$after"
grep -Fxq "xfce: RUNNING $xfce" <<< "$after"
grep -Fq 'x11_desktop: READY' <<< "$after"
echo S31_RUNNING_USER_XFCE=UNCHANGED
echo S31_ANDROID_PHYSICAL_RESIZE=USER_VISUAL_TEST_REQUIRED
