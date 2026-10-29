#!/data/data/com.termux/files/usr/bin/bash
# Build the Mesa 27 Turnip ICD from the SAME PR #25 pinned private Mesa tree.
# Never install the ICD, change /usr/lib, replace Termux-X11 or APK.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
BASE="$ROOT/.source/gpu-stage27-pr25"
PIN="6f90e92dfdacc4759d8e9ccb328805d0f0b4439b"
[[ "$(git -C "$BASE/source" rev-parse HEAD)" == "$PIN" ]] || exit 2
BUILD="$BASE/mesa-private/build"
RUNTIME="$BASE/mesa-private/private-runtime"
[[ -f "$BUILD/build.ninja" ]] || {
    echo 'S14_MESA27_TURNIP=PRIVATE_MESON_SETUP_REQUIRED' >&2;exit 2;
}
target='src/freedreno/vulkan/libvulkan_freedreno.so'
# Drain Ninja's target list entirely: grep -q under pipefail can SIGPIPE
# the Ninja producer even when the target exists.
if ! ninja -C "$BUILD" -t targets all | grep -E "^${target}: " >/dev/null; then
    echo 'S14_MESA27_TURNIP=NO_ICD_TARGET' >&2;exit 2;
fi
ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-2}" "$target"
[[ -s "$BUILD/$target" ]] || exit 2
mkdir -p "$RUNTIME/lib"
cp "$BUILD/$target" "$RUNTIME/lib/libvulkan_freedreno.so"
cmp -s "$BUILD/$target" "$RUNTIME/lib/libvulkan_freedreno.so"
python3 - "$RUNTIME" <<'PY'
from pathlib import Path
import json,sys
base=Path(sys.argv[1]).resolve()
# ICC loader API version field is a loader manifest constraint, not a claim
# about vkEnumerateInstanceVersion or physical device availability.
p={"file_format_version":"1.0.1", "ICD":{
   "library_path":str(base/"lib/libvulkan_freedreno.so"),
   "library_arch":"64", "api_version":"1.4.359"}}
(base/"turnip_icd.private.json").write_text(json.dumps(p,indent=2)+'\n')
PY
echo "S14_MESA27_TURNIP_ICD=$RUNTIME/lib/libvulkan_freedreno.so"
echo "S14_MESA27_TURNIP_JSON=$RUNTIME/turnip_icd.private.json"
echo 'S14_MESA27_TURNIP_STAGING=BUILD_MATCH'
echo 'NOTE: Vulkan device enumeration and AHB Wayland WSI remain unverified.'
