#!/data/data/com.termux/files/usr/bin/bash
# Read-only S36 direct GLAMOR AHB output verification. Never restarts user
# XFCE, Android helper, or changes global Termux-X11/GL/Vulkan drivers.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage1/s36-ahb-output"
BASE="$ROOT/.source/gpu-stage1/xwayland-kgsl/source/hw/xwayland/xwayland-glamor-gbm.c"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0010-kgsl-rootful-ahb-direct-output.patch"
PINNED=df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5

bash -n termux/alroot-wayland graphics/tools/build-xwayland-private.sh \
    graphics/tools/build-s36-rootful-ahb-output.sh \
    graphics/tools/start-s36-rootful-ahb-output.sh
python3 -m py_compile \
    graphics/tools/private_kgsl_xwayland_session.py \
    graphics/tools/relink-s36-rootful-ahb-xwayland.py \
    graphics/tools/test-s36-rootful-ahb-output.py
[[ -s "$BASE" && -s "$PATCH" &&
   "$(sha256sum "$BASE"|cut -d' ' -f1)" == 1f1bd939d026eaa5e1e695488a63c75dcde5af382a4ff3c9f6d1d2642f0aa487 ]] || {
    echo S36_SCOPE=FAILED_UNTRUSTED_BASELINE_GBM_SOURCE >&2;exit 3;
}

tmp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/s36-ahb-verify.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/hw/xwayland"
cp "$BASE" "$tmp/hw/xwayland/xwayland-glamor-gbm.c"
(cd "$tmp" && patch --batch --silent -p1 < "$PATCH")
[[ "$(sha256sum "$tmp/hw/xwayland/xwayland-glamor-gbm.c"|cut -d' ' -f1)" == \
   c1c7d46d7b4825f40906cbe7562e1f5031fa9ac74c203c2dc879714ba9c228d8 ]] || {
    echo S36_SCOPE=FAILED_PRIVATE_SOURCE_PATCH_REPLAY >&2;exit 3;
}
echo S36_S32_TO_DIRECT_AHB_SOURCE_PATCH_REPLAY=PASS

python3 - <<'PY'
from pathlib import Path
gbm = Path('.source/gpu-stage1/s36-ahb-output/scratch/xwayland-glamor-gbm.s36.c').read_text()
mod = Path('graphics/src/s36_xwl_ahb_output.c').read_text()
runner = Path('graphics/tools/test-s36-rootful-ahb-output.py').read_text()
checks = {
    'PRIVATE_ROOTFUL_KGSL_ONLY': '!xwl_screen->rootless' in gbm and
                                  'xwl_glamor_is_kgsl_surfaceless(xwl_screen)' in gbm,
    'ONLY_EXACT_PINNED_1080P': 'width == 1920 && height == 1080 && depth == 24' in gbm,
    'OPTIN_FLAG_REQUIRED': 'S36_KGSL_ROOT_AHB_OUTPUT' in gbm,
    'AHB_IMAGE_FD_VETTED': 'alroot_kgsl_ahb_dup_image_fd' in mod,
    'FBO_USES_DIRECT_AHB_TEXTURE': 'glamor_set_pixmap_texture(pixmap, ahb_texture)' in gbm,
    'NO_SECOND_OUTPUT_BLIT_ON_DIRECT': 'if (out->direct_output)' in mod and
                                        'if (source_texture != out->texture)' in mod,
    'GL_TO_HOST_SYNC': 'glFinish();' in mod,
    'HOST_CUSTOM_GPU_BUFFER': 'alroot_gpu_buffer_v1_create_buffer(' in mod,
    'MANAGER_GENERATION_RESET': 's36_ahb_output_reset(NULL)' in gbm,
    'ORIGINAL_NON_AHB_FALLBACK': 'init_buffer_params_kgsl_surfaceless(' in gbm,
    'KEEP_MESA_DRI3_IMPORT': 'xwl_glamor_kgsl_refresh_dri3_pixmap' in gbm,
    'IMPORTS_PUB_NDK_HANDLE_API': 'AHardwareBuffer_sendHandleToUnixSocket' in mod,
    'CLIENT_DRAW_VERIFICATION': 'S32_VKCUBE_RENDERED_CONTENT_DURING_RESIZE' in runner,
    'NO_CPU_PIXEL_READBACK_IN_SERVER': 'glReadPixels(' not in gbm and
                                       'XGetImage(' not in gbm,
    'NO_SYSTEM_ROOTFS_CHANGE': 'LD_PRELOAD=' not in mod,
}
for name, passed in checks.items():
    print('S36_SOURCE_' + name + '=' + ('PASS' if passed else 'FAIL'))
    if not passed:
        raise SystemExit(4)
PY

declare -a approved=(
    "$ROOT/.source/gpu-stage1/xwayland-kgsl/build/hw/xwayland/Xwayland|f52749b892b4da97f69644eadfaf9c644228b0a3af4a86a13cd792a85a035701"
    "$ROOT/.source/gpu-stage1/s28-kgsl-resize-preserve/runtime/Xwayland|cd1071681188234615ceb75a95ceea73eba89afacd70a5a51dd460f3f67256e7"
    "$ROOT/.source/gpu-stage1/s29-native-resize/runtime/Xwayland|3e2d56bc05ad9d91a5b96a9c93e5eccd8ce6a65493ae3317afd94c1094d6ff2c"
    "$ROOT/.source/gpu-stage1/s30-min-extent/runtime/Xwayland|4caed8ebbf30a7ace6f4b252ab72b0b17a8c8d2c4524c1d77bf787d942d4ea31"
    "$ROOT/.source/gpu-stage1/s31-resize-continuity/runtime/Xwayland|73acf8472faf1e5b91b5f4e998c32ed3322d30fc68e7b0683eef62b6a9c1fe90"
    "$ROOT/.source/gpu-stage1/s32-no-blank-present/runtime/Xwayland|b56b7abacd6f1e1a9f1ba2e453d2038b73eec15cd5cf5d5160eb582876194455"
    "$OUT/runtime/Xwayland|$PINNED"
)
for item in "${approved[@]}";do
    path="${item%%|*}";hash="${item#*|}"
    [[ -x "$path" && ! -L "$path" &&
       "$(sha256sum "$path"|cut -d' ' -f1)" == "$hash" ]] || {
        echo "S36_SCOPE=FAILED_PRIVATE_BINARY_MISMATCH:$path" >&2;exit 5;
    }
done
echo S36_SEVEN_PINNED_PRIVATE_XWAYLAND_EXECUTABLES=PASS

before="$(awstart --status)"
pid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
xfce="$(sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
[[ "$pid" =~ ^[0-9]+$ && "$xfce" =~ ^[0-9]+$ ]] || exit 5
bash graphics/tools/start-s36-rootful-ahb-output.sh --restart --dry-run > "$tmp/s36-preview"
bash graphics/tools/start-s36-rootful-ahb-output.sh --restore-s32 --dry-run > "$tmp/s32-rollback"
bash graphics/tools/start-s36-rootful-ahb-output.sh --restore-s30 --dry-run > "$tmp/s30-rollback"
grep -Fxq 'S36_DRY_RUN_MODE=s36' "$tmp/s36-preview"
grep -Fxq 'S36_DRY_RUN_MODE=s32' "$tmp/s32-rollback"
grep -Fxq 'S36_DRY_RUN_MODE=s30' "$tmp/s30-rollback"
python3 graphics/tools/test-s28-s26-visual-session-guard.py > "$tmp/live-guard"
echo S36_NATIVE_SHELL_SWITCH_ROLLBACK_AND_VISUAL_GUARD=PASS

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$tmp/system-drivers"
echo S36_INSTALLED_TERMUX_X11_AND_GPU_DRIVERS=UNCHANGED
after="$(awstart --status)"
grep -Fxq "xwayland: RUNNING $pid" <<< "$after"
grep -Fxq "xfce: RUNNING $xfce" <<< "$after"
grep -Fq 'x11_desktop: READY' <<< "$after"
echo S36_ACTIVE_XFCE_AND_IME_NOT_RESTARTED=PASS
echo S36_PHYSICAL_ANDROID_DISPLAY_CONTENT=NOT_VERIFIED
