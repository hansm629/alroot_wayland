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
TLS_PATCH="$ROOT/graphics/patches/mesa/egl/0002-bionic-glapi-emutls-symbols.patch"
DRICONF_PATCH="$ROOT/graphics/patches/mesa/egl/0003-zink-optional-driconf-kopper.patch"
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

# Mesa's Bionic TLS globals are exported as __emutls_v.* by Android
# Clang, whereas the upstream version-script names the ELF TLS symbols.
# Overlay only dri.sym.in; never edit the reference Gallium sources.
if [[ -L "$SRC/src/gallium" ]]; then rm "$SRC/src/gallium"; fi
mkdir -p "$SRC/src/gallium/targets/dri"
for file in "$REF/src/gallium"/*; do
    [[ "${file##*/}" == targets ]] && continue
    [[ -e "$SRC/src/gallium/${file##*/}" ]] ||
        ln -s "$file" "$SRC/src/gallium/${file##*/}"
done
for file in "$REF/src/gallium/targets"/*; do
    [[ "${file##*/}" == dri ]] && continue
    [[ -e "$SRC/src/gallium/targets/${file##*/}" ]] ||
        ln -s "$file" "$SRC/src/gallium/targets/${file##*/}"
done
for file in "$REF/src/gallium/targets/dri"/*; do
    [[ "${file##*/}" == dri.sym.in ]] && continue
    [[ -e "$SRC/src/gallium/targets/dri/${file##*/}" ]] ||
        ln -s "$file" "$SRC/src/gallium/targets/dri/${file##*/}"
done
# Optional DRI knobs may be absent on DRM-less Kopper. Keep a private
# copy of the Zink driver source; never patch the user reference repo.
if [[ -L "$SRC/src/gallium/drivers" ]]; then
    rm "$SRC/src/gallium/drivers"
fi
mkdir -p "$SRC/src/gallium/drivers/zink"
for file in "$REF/src/gallium/drivers"/*; do
    [[ "${file##*/}" == zink ]] && continue
    [[ -e "$SRC/src/gallium/drivers/${file##*/}" ]] ||
        ln -s "$file" "$SRC/src/gallium/drivers/${file##*/}"
done
for file in "$REF/src/gallium/drivers/zink"/*; do
    [[ "${file##*/}" == zink_screen.c ]] && continue
    [[ -e "$SRC/src/gallium/drivers/zink/${file##*/}" ]] ||
        ln -s "$file" "$SRC/src/gallium/drivers/zink/${file##*/}"
done
hash="$(sha256sum "$PATCH" "$TLS_PATCH" "$DRICONF_PATCH" | sha256sum | cut -d' ' -f1)"
previous=""
[[ ! -r "$WORK/patch.sha256" ]] || read -r previous < "$WORK/patch.sha256"
if [[ "$hash" != "$previous" ]]; then
    # Remove a stale symlink before replacing with a PRIVATE source file.
    target="$SRC/src/egl/drivers/dri2/platform_wayland.c"
    [[ ! -L "$target" ]] || rm "$target"
    cp "$REF/src/egl/drivers/dri2/platform_wayland.c" "$target"
    (cd "$REF" && git apply --check "$PATCH")
    (cd "$REF" && git apply --check "$TLS_PATCH")
    (cd "$REF" && git apply --check "$DRICONF_PATCH")
    (cd "$SRC" && patch --batch --forward -p1 -i "$PATCH")
    target="$SRC/src/gallium/targets/dri/dri.sym.in"
    [[ ! -L "$target" ]] || rm "$target"
    cp "$REF/src/gallium/targets/dri/dri.sym.in" "$target"
    (cd "$SRC" && patch --batch --forward -p1 -i "$TLS_PATCH")
    target="$SRC/src/gallium/drivers/zink/zink_screen.c"
    [[ ! -L "$target" ]] || rm "$target"
    cp "$REF/src/gallium/drivers/zink/zink_screen.c" "$target"
    (cd "$SRC" && patch --batch --forward -p1 -i "$DRICONF_PATCH")
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

# Termux SysV SHM calls used by the common DRI loader are remapped to
# libandroid-shmem. The library must be retained even though its symbols
# appear AFTER static Gallium objects on the link command.
# These flags are confined to this private Mesa build.
"$WORK/meson" configure "$BUILD" \
    '-Dc_link_args=-Wl,--no-as-needed -landroid-shmem -Wl,--as-needed' \
    '-Dcpp_link_args=-Wl,--no-as-needed -landroid-shmem -Wl,--as-needed'

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
runtime="$WORK/private-runtime"
mkdir -p "$runtime/lib/dri"
cp "$BUILD/src/egl/libEGL_mesa.so" "$runtime/lib/libEGL_mesa.so"
cp "$BUILD/src/gallium/targets/dri/libgallium-26.3.0-devel.so" \
   "$runtime/lib/libgallium-26.3.0-devel.so"
ln -sfn ../libgallium-26.3.0-devel.so "$runtime/lib/dri/zink_dri.so"
python3 - "$runtime" <<'PY'
from pathlib import Path
import json,sys
runtime=Path(sys.argv[1]).resolve()
manifest={"file_format_version":"1.0.0",
          "ICD":{"library_path":str(runtime/"lib/libEGL_mesa.so")}}
(runtime/"egl_vendor.private.json").write_text(
    json.dumps(manifest,indent=2)+"\n")
PY
echo "PRIVATE_EGL_RUNTIME=$runtime"
echo 'NOTE: actual EGL Wayland GPU-buffer import/presentation not yet verified'
