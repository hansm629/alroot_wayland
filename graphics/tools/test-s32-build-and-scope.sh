#!/data/data/com.termux/files/usr/bin/bash
# S32 offline source/ABI integrity and active-XFCE safety. Never restarts the
# user's project desktop, Termux-X11, or modifies installed GPU libraries.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
SOURCE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0009-kgsl-nonblank-present-continuity.patch"

bash -n termux/alroot-wayland \
    graphics/tools/build-xwayland-private.sh \
    graphics/tools/build-s32-kgsl-nonblank-present.sh \
    graphics/tools/start-s32-kgsl-nonblank-present.sh
python3 -m py_compile \
    graphics/tools/private_kgsl_xwayland_session.py \
    graphics/tools/relink-s32-private-xwayland.py \
    graphics/tools/test-s32-nonblank-resize.py

python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
window = (p / 'xwayland-window.c').read_text()
present = (p / 'xwayland-present.c').read_text()
gbm = (p / 'xwayland-glamor-gbm.c').read_text()
checks = {
  'STRICT_S32_OPTIN': 'getenv("S32_KGSL_NO_BLANK_PRESENT")' in window,
  'NO_S31_STALE_WHOLE_WINDOW_CLEAR': 'if (stale_extent && !s32)' in window,
  'REAL_RENDERED_CONTENT_RETAINED': 'if (sw < w)' in window and
                                       'if (sh < h)' in window,
  'NO_SNAPSHOT_REPLAY_AFTER_PRESENT': 'S32_KGSL_SKIP_REPLAY' not in window,
  'MATCHED_PRESENT_RETIRES_SNAPSHOT': 'if (sw != w || sh < h || sh - h > 3)' in window,
  'NONMATCHING_PRESENT_KEEPS_SNAPSHOT': 'if (sw != w || sh < h || sh - h > 3)\n            return;' in window,
  'REAL_CLIENT_DRI3_NOT_MODIFIED': 'if (s32 && xwl_glamor_kgsl_is_dri3_pixmap(screen->GetWindowPixmap(window)))' in window,
  'NO_S28_BILINEAR_STRETCH': 'SetPictureFilter(src, "bilinear"' not in window,
  'KGSL_ONLY_NO_ROOTLESS': 'xwl_screen->rootless' in window and
                            'xwl_glamor_is_kgsl_surfaceless(xwl_screen)' in window,
  'MESA_XCB_CLIENT_MARKER': 'MakeAtom("_MESA_DRV", 9, FALSE)' in window,
  'STABLE_WINDOW_LOCAL_COPY': 's30_capture_stable_frame(window, &window->drawable)' in window,
  'PRESENT_COPY_AND_FLIP_RETAINED': present.count('xwl_window_s30_real_present(window, vblank->pixmap)') == 2,
  'S30_TINY_AHB_CORRECTION_RETAINED': 'S30_KGSL_TINY_AHB_FD0' in gbm,
  'NO_SERVER_CPU_PIXEL_READBACK': 'XGetImage(' not in window and 'glReadPixels(' not in window,
}
for name, passed in checks.items():
    print('S32_SOURCE_'+name+'='+('PASS' if passed else 'FAIL'))
    if not passed:
        raise SystemExit(3)
PY

declare -a names=(xwayland-window.c xwayland-window.h xwayland-present.c)
declare -a after_sha=(
    85b3d7cda893a679ca92fc5958100e837d6ab6f6870049792076a03f6d3ed9ea
    378bdbefb30e96515079e6e29251dc769ce2b03dcac2a0353ea859d7dac1e7fd
    fae115e07784e39685ead8da8152ffc5af7c51b26e2fa1f4ddc2d4fcdcb3c73b
)
for i in "${!names[@]}";do
    path="$SOURCE/${names[$i]}"
    [[ -f "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "${after_sha[$i]}" ]] || {
        echo S32_SCOPE=FAILED_PRIVATE_SOURCE_PIN >&2;exit 3;
    }
done
echo S32_PRIVATE_SOURCE_HASHES=PASS

[[ -s "$PATCH" ]] || { echo S32_SCOPE=FAILED_NO_REPRODUCIBLE_PATCH >&2;exit 3; }
tmp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/s32-no-blank-replay.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/hw/xwayland"
for file in "${names[@]}";do cp -p "$SOURCE/$file" "$tmp/hw/xwayland/$file";done
(cd "$tmp" && patch --batch --silent -R -p1 < "$PATCH")
for spec in \
    'xwayland-window.c|9d422b27e55d9848f3e8341402a9bb966d7a448d2ce5e38b8cf8e61ba466ac77' \
    'xwayland-window.h|2e0751cd815130255f72fb0fc709370ef38b6ac8f6db477f2ac131bfda844809' \
    'xwayland-present.c|49195bcf51e8a9fff9f0ef17e85e9e459518b982bd1ee9ea1984162da20a3575';do
    file="${spec%%|*}";expected="${spec#*|}"
    [[ "$(sha256sum "$tmp/hw/xwayland/$file"|cut -d' ' -f1)" == "$expected" ]] || {
        echo S32_SCOPE=FAILED_S31_BASELINE_PROVENANCE >&2;exit 3;
    }
done
(cd "$tmp" && patch --batch --silent -p1 < "$PATCH")
for file in "${names[@]}";do cmp "$tmp/hw/xwayland/$file" "$SOURCE/$file";done
echo S32_S31_BASELINE_PATCH_BYTE_REPLAY=PASS

declare -a approved=(
    "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$ROOT/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
    "$ROOT/.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland|b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455"
)
for spec in "${approved[@]}";do
    path="${spec%%|*}";expected="${spec#*|}"
    [[ -x "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "$expected" ]] || {
        echo S32_SCOPE=FAILED_PRIVATE_EXECUTABLE_HASH >&2;exit 4;
    }
done
echo S32_ALL_SIX_PRIVATE_XWAYLAND_PINNED=PASS

before="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
xfce_pid="$(sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
[[ "$pid" =~ ^[0-9]+$ && "$xfce_pid" =~ ^[0-9]+$ ]] || exit 4
bash graphics/tools/start-s32-kgsl-nonblank-present.sh --restart --dry-run > "$tmp/s32-preview.log"
bash graphics/tools/start-s32-kgsl-nonblank-present.sh --restore-s30 --dry-run > "$tmp/s30-rollback.log"
grep -Fxq 'S32_DRY_RUN_MODE=s32' "$tmp/s32-preview.log"
grep -Fxq 'S32_DRY_RUN_MODE=s30' "$tmp/s30-rollback.log"
python3 graphics/tools/test-s28-s26-visual-session-guard.py \
    > "$tmp/s26-visual-guard.log"
echo S32_SAFE_EXPLICIT_SWITCH_ROLLBACK_AND_S26_VISUAL_GUARD=PASS

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$tmp/protected-system-drivers.log"
echo S32_GLOBAL_TERMUX_X11_AND_GPU_DRIVERS=UNCHANGED
after="$(awstart --status)"
grep -Fxq "xwayland: RUNNING $pid" <<< "$after"
grep -Fxq "xfce: RUNNING $xfce_pid" <<< "$after"
grep -Fq 'x11_desktop: READY' <<< "$after"
echo S32_RUNNING_USER_XFCE_UNCHANGED=PASS
echo S32_PHYSICAL_ANDROID_VISUAL=USER_ACCEPTANCE_REQUIRED
