#!/data/data/com.termux/files/usr/bin/bash
# Build an Alroot-only Mesa 27 PR #25 Zink/KGSL/Turnip frontend in an isolated overlay.
# The reference Mesa source, installed libEGL/libGLES/libGL and all Termux
# X11/DRI3 Vulkan drivers remain entirely unchanged.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
REF="$ROOT/.source/gpu-stage27-pr25/source"
PIN="6f90e92dfdacc4759d8e9ccb328805d0f0b4439b"
[[ "$(git -C "$REF" rev-parse HEAD)" == "$PIN" && "$(cat "$REF/VERSION")" == "27.0.0-devel" ]] || {
  echo "S14_MESA27_PROVENANCE=FAIL expected $PIN" >&2
  exit 2
}
echo "S14_MESA27_PIN=$PIN"
# S26U runs Android API 37. The Termux C compiler defaults to API 24,
# which makes sync_merge (introduced in API 26) unavailable at compile
# time. This *private* test build targets Android 35; no global flags or
# supported APK minSdk level are modified.
DEVICE_API="$(getprop ro.build.version.sdk 2>/dev/null || true)"
[[ "$DEVICE_API" =~ ^[0-9]+$ ]] && ((DEVICE_API>=35)) || {
  echo "S14_MESA27_PRIVATE_TARGET=REQUIRES_ANDROID35 device=$DEVICE_API" >&2
  exit 2
}
echo "S14_MESA27_PRIVATE_TARGET=android35 device=$DEVICE_API"
WORK="$ROOT/.source/gpu-stage27-pr25/mesa-private"
SRC="$WORK/source"
BUILD="$WORK/build"
PATCH="$ROOT/graphics/patches/mesa/egl/0001-zink-kopper-ahb-wayland-no-rendernode.patch"
TLS_PATCH="$ROOT/graphics/patches/mesa/egl/0002-bionic-glapi-emutls-symbols.patch"
DRICONF_PATCH="$ROOT/graphics/patches/mesa/egl/0003-zink-optional-driconf-kopper.patch"
TERMUX_INCLUDE_PATCH="$ROOT/graphics/patches/mesa/pr25/0004-do-not-check-xlocale.patch"
TURNIP_MUTEX_PATCH="$ROOT/graphics/patches/mesa/pr25/0007-use-mtx_t-operations-in-turnip.patch"
TERMUX_EPOLL_PATCH="$ROOT/graphics/patches/mesa/pr25/0022-fix-epoll-header-inclusion.patch"
PR25_PATCH_DIR="$ROOT/graphics/patches/mesa/pr25"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"

# Keep PR #25's package-local patches byte-exact, rather than accidentally
# mixing a newer termux-packages branch with the pinned Mesa 27 source.
for check in \
 "b08f31284e18bc03509cdd0a88eb4b1c1e247032140467a6dfaa92c970250886 $TERMUX_INCLUDE_PATCH" \
 "27b7bc421d1981d0e26ddaceac800b3dbb4e3c8b038b236d14f0aef7b2289203 $TURNIP_MUTEX_PATCH" \
 "8323e7e69e9d8b238985be95fc410a9efeb4f2ae4a234228071efcc86614d998 $TERMUX_EPOLL_PATCH"; do
  expected="${check%% *}"
  file="${check#* }"
  [[ "$(sha256sum "$file" | cut -d' ' -f1)" == "$expected" ]] || {
    echo "S14_MESA27_PR25_PATCH_INTEGRITY=FAIL $file" >&2
    exit 2
  }
done
sha256sum -c "$PR25_PATCH_DIR/SHA256SUMS" >/dev/null || {
  echo 'S14_MESA27_PR25_PATCHSET=INTEGRITY_FAILED' >&2
  exit 2
}
echo 'S14_MESA27_PR25_PATCHSET=14_VERIFIED'

test -f "$REF/meson.build" && test -f "$REF/src/egl/drivers/dri2/platform_wayland.c"
mkdir -p "$WORK" "$SRC/src/egl/drivers/dri2"
# Link source files read-only into an isolated overlay; only platform_wayland
# gets a physical copy. Nothing from the reference source is writable.
for file in "$REF"/*; do
    [[ "${file##*/}" == src ]] && continue
    [[ -e "$SRC/${file##*/}" ]] || ln -s "$file" "$SRC/${file##*/}"
done

# Termux's public NDK sysroot does not install AOSP's private
# <cutils/native_handle.h>. Mesa's pinned Android stub already provides
# its declaration. Expose that source header inside the private include
# overlay; do not install a fake cutils header or alter the system SDK.
if [[ -L "$SRC/include" ]]; then
    rm "$SRC/include"
    mkdir "$SRC/include"
    for file in "$REF/include"/*; do
        ln -s "$file" "$SRC/include/${file##*/}"
    done
fi
mkdir -p "$SRC/include/cutils"
ln -sfn "$REF/include/android_stub/cutils/native_handle.h" \
  "$SRC/include/cutils/native_handle.h"
echo 'S14_MESA27_AOSP_HEADER=PRIVATE_ANDROID_STUB'
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

# A private DRI version script is needed only to isolate possible TLS
# adjustments from the immutable reference tree. Unlike the old Mesa 26.3
# API24 build, Mesa 27 here targets Android API35 and uses genuine ELF TLS.
# Applying the 26.3 __emutls_v export patch at API35 is WRONG: it causes
# missing-symbol failures at link time. Keep the original ELF TLS names.
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

# Turn the minimum set of reference symlinks into PRIVATE source copies.
# A nested symlink must never be passed directly to patch(1), because doing
# so risks changing the pinned source checkout or an installed driver.
private_source_file() {
  local relative="$1" part target_ref="$REF" target_src="$SRC"
  local -a parts
  IFS=/ read -r -a parts <<< "$relative"
  for ((i=0;i<${#parts[@]}-1;i++)); do
    part="${parts[i]}"
    target_ref+="/$part"
    target_src+="/$part"
    if [[ -L "$target_src" ]]; then
      rm "$target_src"
      mkdir "$target_src"
      local item
      for item in "$target_ref"/*; do
        ln -s "$item" "$target_src/${item##*/}"
      done
    fi
  done
  target_src="$SRC/$relative"
  if [[ -L "$target_src" ]]; then rm "$target_src"; fi
  cp "$REF/$relative" "$target_src"
}

hash="$( { sha256sum "$PATCH" "$TLS_PATCH" "$DRICONF_PATCH" \
  "$PR25_PATCH_DIR"/*.patch; echo 'S14_ANDROID35_ELF_TLS_NO_EMUTLS_PATCH'; } \
  | sha256sum | cut -d' ' -f1)"
previous=""
[[ ! -r "$WORK/patch.sha256" ]] || read -r previous < "$WORK/patch.sha256"
if [[ "$hash" != "$previous" ]]; then
    # Remove a stale symlink before replacing with a PRIVATE source file.
    target="$SRC/src/egl/drivers/dri2/platform_wayland.c"
    [[ ! -L "$target" ]] || rm "$target"
    cp "$REF/src/egl/drivers/dri2/platform_wayland.c" "$target"
    (cd "$REF" && git apply --check "$PATCH")
    (cd "$REF" && git apply --check "$DRICONF_PATCH")
    (cd "$SRC" && patch --batch --forward -p1 -i "$PATCH")
    target="$SRC/src/gallium/targets/dri/dri.sym.in"
    [[ ! -L "$target" ]] || rm "$target"
    cp "$REF/src/gallium/targets/dri/dri.sym.in" "$target"
    echo 'S14_MESA27_TLS=ELF_TLS_NATIVE_ANDROID35'
    target="$SRC/src/gallium/drivers/zink/zink_screen.c"
    [[ ! -L "$target" ]] || rm "$target"
    cp "$REF/src/gallium/drivers/zink/zink_screen.c" "$target"
    (cd "$SRC" && patch --batch --forward -p1 -i "$DRICONF_PATCH")

    # The PR25 packaging patches are a required part of the user-selected
    # baseline, not just the upstream 6f90e92 Git tree. Patch only copied
    # files in this overlay. Patch 0012's stock Android Wayland branch
    # conflicts with Alroot Kopper; preserve its Android software device
    # semantics while retaining Kopper's DRM-less behaviour below.
    x11only="$WORK/pr25-0012-x11-only.patch"
    sed '/^--- a\/src\/egl\/drivers\/dri2\/platform_wayland.c/,$d' \
      "$PR25_PATCH_DIR/0012-always-use-software-for-swrast.patch" > "$x11only"
    mapfile -t pkg_files < <(
      for pkg in "$PR25_PATCH_DIR"/*.patch; do
        if [[ "${pkg##*/}" == 0012-* ]]; then pkg="$x11only"; fi
        awk '/^\+\+\+ / { print $2 }' "$pkg"
      done | sed -e 's#^b/##' -e 's#^\./##' | grep -v '^/dev/null$' | sort -u
    )
    for rel in "${pkg_files[@]}"; do
      [[ "$rel" != src/egl/drivers/dri2/platform_wayland.c ]] || {
        echo 'S14_PR25_WAYLAND_PATCH=CONFLICT_REQUIRES_EXPLICIT_MERGE' >&2
        exit 2
      }
      private_source_file "$rel"
    done
    for pkg in "$PR25_PATCH_DIR"/*.patch; do
      if [[ "${pkg##*/}" == 0012-* ]]; then pkg="$x11only"; fi
      (cd "$SRC" && patch --batch --forward --fuzz=2 -p1 < "$pkg")
    done
    python3 - "$SRC/src/egl/drivers/dri2/platform_wayland.c" <<'PY'
from pathlib import Path
import sys
file=Path(sys.argv[1]); src=file.read_text()
needle='''   if (!dri2_setup_device(disp, disp->Options.ForceSoftware ||
                         (dri2_dpy->kopper && dri2_dpy->fd_render_gpu < 0))) {'''
assert src.count(needle)==1, 'Mesa27 PR25/Alroot Kopper path no longer matches'
merged='''#ifdef __ANDROID__
   if (!dri2_setup_device(disp, true)) {
#else
   if (!dri2_setup_device(disp, disp->Options.ForceSoftware ||
                         (dri2_dpy->kopper && dri2_dpy->fd_render_gpu < 0))) {
#endif'''
file.write_text(src.replace(needle,merged))
PY
    echo 'S14_MESA27_PR25_PATCHES=14_APPLIED_PRIVATE_ONLY'
    echo 'S14_MESA27_PR25_SWRAST_WAYLAND=MERGED_WITH_ALROOT_KOPPER'
    printf '%s\n' "$hash" > "$WORK/patch.sha256"
fi

# The private build MUST NOT overwrite Termux system libs or the known-good
# 26.3.0-devel private runtime. No package manager writes are performed.
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
        -Dgallium-drivers=zink,freedreno \
        -Dvulkan-drivers=freedreno \
        -Dfreedreno-kmds=msm,kgsl \
        -Dmesa-clc=auto \
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
    '-Dc_args=--target=aarch64-linux-android35' \
    '-Dcpp_args=--target=aarch64-linux-android35' \
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
echo "S14_MESA27_EGL_TARGET=$target"
ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-2}" "$target"
echo 'PASS: private Mesa27 Zink+KGSL EGL frontend built with Alroot Kopper adapter'
runtime="$WORK/private-runtime"
mkdir -p "$runtime/lib/dri"
cp "$BUILD/src/egl/libEGL_mesa.so" "$runtime/lib/libEGL_mesa.so"
cp "$BUILD/src/gallium/targets/dri/libgallium-27.0.0-devel.so" \
   "$runtime/lib/libgallium-27.0.0-devel.so"
cmp -s "$BUILD/src/egl/libEGL_mesa.so" "$runtime/lib/libEGL_mesa.so"
cmp -s "$BUILD/src/gallium/targets/dri/libgallium-27.0.0-devel.so" \
       "$runtime/lib/libgallium-27.0.0-devel.so"
echo 'S14_MESA27_STAGING=BUILD_MATCH'
ln -sfn ../libgallium-27.0.0-devel.so "$runtime/lib/dri/zink_dri.so"
ln -sfn ../libgallium-27.0.0-devel.so "$runtime/lib/dri/kgsl_dri.so"

python3 - "$runtime" <<'PY'
from pathlib import Path
import json,sys
runtime=Path(sys.argv[1]).resolve()
manifest={"file_format_version":"1.0.0",
          "ICD":{"library_path":str(runtime/"lib/libEGL_mesa.so")}}
(runtime/"egl_vendor.private.json").write_text(
    json.dumps(manifest,indent=2)+"\n")
PY
echo "S14_MESA27_RUNTIME=$runtime"
echo 'NOTE: actual EGL Wayland GPU-buffer import/presentation not yet verified'
