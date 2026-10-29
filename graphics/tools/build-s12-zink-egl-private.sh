#!/data/data/com.termux/files/usr/bin/bash
# Build an Alroot-only Mesa EGL/Zink/Kopper frontend in an overlay checkout.
# The reference Mesa source, installed libEGL/libGLES/libGL and all Termux
# X11/DRI3 Vulkan drivers remain entirely unchanged.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
REF="${ALROOT_WRAPPER_SOURCE:-$HOME/alroot-vk-lab/vulkan-wrapper-android}"
WORK="$ROOT/.source/gpu-stage2/mesa-egl"
SRC="$WORK/source"
BUILD="$WORK/build"
PATCH="$ROOT/graphics/patches/mesa/egl/0001-zink-kopper-ahb-wayland-no-rendernode.patch"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"

test -f "$REF/meson.build" && test -f "$REF/src/egl/drivers/dri2/platform_wayland.c"
mkdir -p "$WORK" "$SRC/src/egl/drivers/dri2"
# Link source files read-only into an isolated overlay; only platform_wayland
# gets a physical copy. Nothing from the reference source is writable.
for file in "$REF"/*; do
    [[ "${file##*/}" == src ]] && continue
    [[ -e "$SRC/${file##*/}" ]] || ln -s "$file" "$SRC/${file##*/}"
done
for file in "$REF/src"/*; do
    [[ "${file##*/}" == egl ]] && continue
    [[ -e "$SRC/src/${file##*/}" ]] || ln -s "$file" "$SRC/src/${file##*/}"
done
for file in "$REF/src/egl"/*; do
    [[ "${file##*/}" == drivers ]] && continue
    [[ -e "$SRC/src/egl/${file##*/}" ]] || ln -s "$file" "$SRC/src/egl/${file##*/}"
done
for file in "$REF/src/egl/drivers"/*; do
    [[ "${file##*/}" == dri2 ]] && continue
    [[ -e "$SRC/src/egl/drivers/${file##*/}" ]] ||
        ln -s "$file" "$SRC/src/egl/drivers/${file##*/}"
done
for file in "$REF/src/egl/drivers/dri2"/*; do
    [[ "${file##*/}" == platform_wayland.c ]] && continue
    [[ -e "$SRC/src/egl/drivers/dri2/${file##*/}" ]] ||
        ln -s "$file" "$SRC/src/egl/drivers/dri2/${file##*/}"
done
hash="$(sha256sum "$PATCH" | cut -d' ' -f1)"
previous=""
[[ ! -r "$WORK/patch.sha256" ]] || read -r previous < "$WORK/patch.sha256"
if [[ "$hash" != "$previous" ]]; then
    # Remove a stale symlink before replacing with a PRIVATE source file.
    target="$SRC/src/egl/drivers/dri2/platform_wayland.c"
    [[ ! -L "$target" ]] || rm "$target"
    cp "$REF/src/egl/drivers/dri2/platform_wayland.c" "$target"
    (cd "$REF" && git apply --check "$PATCH")
    (cd "$SRC" && patch --batch --forward -p1 -i "$PATCH")
    printf '%s\n' "$hash" > "$WORK/patch.sha256"
fi

# The Mesa Python module exists even when the Termux meson executable is
# absent. An absolute script ensures Ninja regeneration uses a stable path.
cat > "$WORK/meson" <<'PY'
#!/data/data/com.termux/files/usr/bin/python3
from mesonbuild.mesonmain import main
import sys
sys.exit(main())
PY
chmod 700 "$WORK/meson"

if [[ ! -f "$BUILD/build.ninja" ]]; then
    "$WORK/meson" setup "$BUILD" "$SRC" \
        --prefix="$WORK/prefix" \
        -Dgallium-drivers=zink \
        -Dvulkan-drivers= \
        -Dplatforms=x11,wayland \
        -Degl=enabled \
        -Dgles2=enabled \
        -Dglx=disabled \
        -Dgbm=disabled \
        -Dllvm=disabled \
        -Dbuild-tests=false \
        -Dgallium-va=disabled \
        -Dvideo-codecs=
fi

# Compile only the EGL vendor frontend + GLES loader first; this exposes
# C or ABI errors before deciding how to package other Gallium objects.
target="$(ninja -C "$BUILD" -t targets all | \
          grep -E '^src/egl/libEGL_mesa\.so: ' | head -1 | cut -d: -f1)"
if [[ -z "$target" ]]; then
    echo 'Mesa did not configure an EGL vendor target; cannot claim support' >&2
    exit 2
fi
echo "PRIVATE_EGL_TARGET=$target"
ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-2}" "$target"
echo 'PASS: private Mesa Zink EGL vendor library compiled with Alroot Kopper adapter'
echo 'NOTE: actual EGL Wayland GPU-buffer import/presentation not yet verified'
