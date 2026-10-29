#!/data/data/com.termux/files/usr/bin/bash
# S1.5: private Mesa 27 Turnip with an opt-in, vendor-neutral AHB GPU-copy
# Wayland WSI. Does not replace S1.4, Termux system Mesa, Termux-X11, or APK.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
BASE="$ROOT/.source/gpu-stage27-pr25"
PIN=6f90e92dfdacc4759d8e9ccb328805d0f0b4439b
[[ "$(git -C "$BASE/source" rev-parse HEAD)" == "$PIN" &&
    "$(cat "$BASE/source/VERSION")" == '27.0.0-devel' ]] || exit 2
sha256sum -c graphics/patches/mesa/pr25/SHA256SUMS >/dev/null
bash graphics/tools/build-s14-mesa27-private.sh >"$BASE/s15-recheck-s14-build.log"
python3 graphics/tools/prepare-s15-turnip-wsi-overlay.py
WORK="$BASE/s15-turnip"
SRC="$WORK/source"
BUILD="$WORK/build"
RUNTIME="$WORK/private-runtime"
cat > "$WORK/meson" <<'PY'
#!/data/data/com.termux/files/usr/bin/python3
from mesonbuild.mesonmain import main
import sys
sys.exit(main())
PY
chmod 700 "$WORK/meson"
if [[ ! -f "$BUILD/build.ninja" ]]; then
    "$WORK/meson" setup "$BUILD" "$SRC" --prefix="$WORK/prefix" \
      -Dgallium-drivers= \
      -Dvulkan-drivers=freedreno \
      -Dfreedreno-kmds=msm,kgsl \
      -Dmesa-clc=auto \
      -Dplatforms=x11,wayland \
      -Degl=disabled -Dgles2=disabled -Dglx=disabled \
      -Dgbm=disabled -Dllvm=disabled -Dbuild-tests=false \
      -Dgallium-va=disabled -Dvideo-codecs=
fi
"$WORK/meson" configure "$BUILD" \
   '-Dc_args=--target=aarch64-linux-android35' \
   '-Dcpp_args=--target=aarch64-linux-android35' \
   '-Dc_link_args=-Wl,--no-as-needed -landroid-shmem -Wl,--as-needed' \
   '-Dcpp_link_args=-Wl,--no-as-needed -landroid-shmem -Wl,--as-needed'
TARGET='src/freedreno/vulkan/libvulkan_freedreno.so'
ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-3}" "$TARGET"
mkdir -p "$RUNTIME/lib"
cp "$BUILD/$TARGET" "$RUNTIME/lib/libvulkan_freedreno.so"
cmp -s "$BUILD/$TARGET" "$RUNTIME/lib/libvulkan_freedreno.so"
python3 - "$RUNTIME" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]).resolve()
(p/'turnip_alroot_icd.private.json').write_text(json.dumps({
 'file_format_version':'1.0.1',
 'ICD':{'library_path':str(p/'lib/libvulkan_freedreno.so'),
        'library_arch':'64','api_version':'1.4.359'}},indent=2)+'\n')
PY
printf 'S15_MESA27_TURNIP_AHB_ICD=%s\n' "$RUNTIME/turnip_alroot_icd.private.json"
echo 'S15_MESA27_TURNIP_AHB_STAGING=BUILD_MATCH'
echo 'NOTE: Actual AHB swapchain pixels and host frame import require real WSI tests.'
