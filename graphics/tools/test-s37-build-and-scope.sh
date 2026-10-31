#!/data/data/com.termux/files/usr/bin/bash
# S37 protects the user's live S36 XFCE and tests only the pinned S26U
# DRI3 narrow-height AHB correction, with S36 output behavior preserved.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage1/s37-thin-ahb"
S36="$ROOT/.source/gpu-stage1/s36-ahb-output"
PATCH="$ROOT/graphics/patches/xwayland/kgsl/0011-kgsl-thin-ahb-fd0.patch"
BASE="$S36/scratch/xwayland-glamor-gbm.s36.c"
SOURCE="$OUT/scratch/xwayland-glamor-gbm.s37.c"

bash -n termux/alroot-wayland graphics/tools/build-s37-kgsl-thin-ahb.sh \
    graphics/tools/start-s36-rootful-ahb-output.sh \
    graphics/tools/start-s37-kgsl-thin-ahb.sh
python3 -m py_compile \
    graphics/tools/test-s36-rootful-ahb-output.py \
    graphics/tools/relink-s37-kgsl-thin-ahb.py \
    graphics/tools/private_kgsl_xwayland_session.py

python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text()
guards = {
    'STRICT_RUNTIME_OPTIN': 'getenv("S37_KGSL_THIN_AHB_FD0")' in s,
    'ONLY_BGRA8_S26U': 'desc.format == 5 && width > 64' in s,
    'THIN_HEIGHT_BOUND': 'height >= 1 && height <= 128' in s,
    'SMALL_BACKING_ONLY': 'expected <= 2ULL * 1024 * 1024' in s,
    'EXACT_TWO_GRALLOC_FDS': 'handle->numFds == 2' in s,
    'FD_SIZE_TIE_REQUIRED': 'img.st_size == meta.st_size' in s,
    'FD_INODES_DIFFER': 'img.st_ino != meta.st_ino' in s,
    'FD0_PLAUSIBLE_LENGTH': 'img.st_size >= (off_t)expected' in s,
    'NO_UNSUPPORTED_S_ISREG_ASSUMPTION': 'S_ISREG(img.st_mode)' not in s,
    'ORIGINAL_S30_NARROW_WIDTH_STILL_AVAILABLE': 'S30_KGSL_TINY_AHB_FD0' in s,
    'ACTUAL_AHB_NATIVE_SOCKET_IMPORT': 'AHardwareBuffer_recvHandleFromUnixSocket' in s,
    'GLAMOR_ROOT_DIRECT_AHB_UNCHANGED': 'S36_KGSL_ROOT_AHB_DIRECT_GLAMOR_FBO=ACTIVE' in s,
    'NO_CPU_PIXEL_READBACK': 'glReadPixels(' not in s and 'XGetImage(' not in s,
}
for label, ok in guards.items():
    print('S37_SCOPE_'+label+'='+('PASS' if ok else 'FAIL'))
    if not ok:
        raise SystemExit(3)
PY

[[ -s "$PATCH" && -s "$BASE" && -s "$SOURCE" && ! -L "$SOURCE" ]] || exit 3
[[ "$(sha256sum "$BASE"|cut -d' ' -f1)" == \
    c1c7d46d7b4825f40906cbe7562e1f5031fa9ac74c203c2dc879714ba9c228d8 ]] || exit 3
tmp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/s37-source-audit.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/hw/xwayland"
cp "$BASE" "$tmp/hw/xwayland/xwayland-glamor-gbm.c"
(cd "$tmp" && patch -s --batch -p1 < "$PATCH")
cmp "$tmp/hw/xwayland/xwayland-glamor-gbm.c" "$SOURCE"
[[ "$(sha256sum "$SOURCE"|cut -d' ' -f1)" == \
    b2a84ec1927f2a7154c1689fa223b531d5d30c395cfa6d0036318cd701a10570 ]] || exit 3
echo S37_S36_TO_S37_SOURCE_PATCH_BYTE_EXACT=PASS

[[ "$(sha256sum "$S36/runtime/Xwayland"|cut -d' ' -f1)" == \
   df7c98daf732cbc735646e0fa92ca3ae9689a681952d3783acb7c7b1cae7e9f5 ]] || exit 4
[[ "$(sha256sum "$OUT/runtime/Xwayland"|cut -d' ' -f1)" == \
   193f43e03f0e754c39cfb73e04ac1e0032f8f632e6d06b5d1b675f67c834a594 ]] || exit 4
readelf -l "$OUT/runtime/Xwayland" | grep -Fq '/system/bin/linker64'
echo S37_PRIVATE_XWAYLAND_BIONIC_ABI_PINNED=PASS

before="$(awstart --status)"
xpid="$(sed -nE 's/^xwayland: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
xfce="$(sed -nE 's/^xfce: RUNNING ([0-9]+)$/\1/p' <<< "$before")"
[[ "$xpid" =~ ^[0-9]+$ && "$xfce" =~ ^[0-9]+$ ]] || exit 4
bash graphics/tools/start-s37-kgsl-thin-ahb.sh --restart --dry-run > "$tmp/s37-dry"
bash graphics/tools/start-s37-kgsl-thin-ahb.sh --restore-s36 --dry-run > "$tmp/s36-rollback"
bash graphics/tools/start-s37-kgsl-thin-ahb.sh --restore-s32 --dry-run > "$tmp/s32-rollback"
grep -Fxq 'S36_DRY_RUN_MODE=s37' "$tmp/s37-dry"
grep -Fxq 'S36_DRY_RUN_MODE=s36' "$tmp/s36-rollback"
grep -Fxq 'S36_DRY_RUN_MODE=s32' "$tmp/s32-rollback"
python3 graphics/tools/test-s28-s26-visual-session-guard.py > "$tmp/visual-guard"
echo S37_OPTIN_S36_AND_S32_ROLLBACK_GUARDS=PASS

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" > "$tmp/gpu-guard"
echo S37_SYSTEM_TERMUX_X11_AND_GPU_DRIVERS=UNCHANGED
after="$(awstart --status)"
grep -Fxq "xwayland: RUNNING $xpid" <<< "$after"
grep -Fxq "xfce: RUNNING $xfce" <<< "$after"
grep -Fq 'x11_desktop: READY' <<< "$after"
echo S37_USER_XFCE_AND_IME=UNCHANGED
echo S37_GLIBC_CLIENT_WSI_AND_PHYSICAL_ANDROID_VISUAL=NOT_VERIFIED_BY_THIS_TEST
