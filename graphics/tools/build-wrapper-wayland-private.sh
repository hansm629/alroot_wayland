#!/data/data/com.termux/files/usr/bin/bash
# Isolated Vulkan wrapper WSI build without touching the user's reference,
# installed Termux ICDs, Vulkan JSON, Termux-X11 DRI3 or active desktop.
# Rebuild only the vulkan_wsi Meson target after applying our opt-in patch.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
REF="${ALROOT_WRAPPER_SOURCE:-$HOME/alroot-vk-lab/vulkan-wrapper-android}"
WORK="$ROOT/.source/gpu-stage1/wrapper-meson"
SRC="$WORK/source"
BUILD="$WORK/build"
PATCH="$ROOT/graphics/patches/mesa/wrapper/0001-alroot-ahb-wayland-wsi-vkimage.patch"
SYNC_PATCH="$ROOT/graphics/patches/mesa/wrapper/0002-alroot-ahb-no-dmabuf-sync.patch"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
test -f "$REF/meson.build" && test -f "$REF/src/vulkan/wsi/wsi_common_wayland.c"
mkdir -p "$WORK" "$SRC/src/vulkan/wsi"

# Mirror the Mesa tree with read-only symlinks. Only the four patch targets
# are real copies in the isolated source tree. Do not create a Git worktree
# or modify even the reference checkout's .git metadata.
for f in "$REF"/*; do
    [[ "$(basename "$f")" == src ]] && continue
    [[ -e "$SRC/$(basename "$f")" ]] ||
        ln -s "$f" "$SRC/$(basename "$f")"
done
for f in "$REF/src"/*; do
    [[ "$(basename "$f")" == vulkan ]] && continue
    [[ -e "$SRC/src/$(basename "$f")" ]] ||
        ln -s "$f" "$SRC/src/$(basename "$f")"
done
mkdir -p "$SRC/src/vulkan"
for f in "$REF/src/vulkan"/*; do
    [[ "$(basename "$f")" == wsi ]] && continue
    [[ -e "$SRC/src/vulkan/$(basename "$f")" ]] ||
        ln -s "$f" "$SRC/src/vulkan/$(basename "$f")"
done
for f in "$REF/src/vulkan/wsi"/*; do
    [[ "$(basename "$f")" == meson.build ||
       "$(basename "$f")" == wsi_common_wayland.c ||
       "$(basename "$f")" == wsi_common.c ]] && continue
    [[ -e "$SRC/src/vulkan/wsi/$(basename "$f")" ]] ||
        ln -s "$f" "$SRC/src/vulkan/wsi/$(basename "$f")"
done
current_patch_hash="$(sha256sum "$PATCH" "$SYNC_PATCH" | sha256sum | cut -d' ' -f1)"
previous_patch_hash=""
if [[ -r "$WORK/.alroot-wsi-patch.sha256" ]]; then
    read -r previous_patch_hash < "$WORK/.alroot-wsi-patch.sha256"
fi
if [[ ! -f "$SRC/src/vulkan/wsi/alroot-gpu-buffer-v1-protocol.c" ||
      "$previous_patch_hash" != "$current_patch_hash" ]]; then
    # Regenerate the disposable overlay on any patch update. This avoids
    # accidentally testing a stale WSI library after changing the patch.
    cp "$REF/src/vulkan/wsi/meson.build" "$SRC/src/vulkan/wsi/"
    cp "$REF/src/vulkan/wsi/wsi_common_wayland.c" "$SRC/src/vulkan/wsi/"
    # If this is an older overlay, remove the symlink *before* copying.
    # Never overwrite a symlink into the user's reference Mesa tree.
    if [[ -L "$SRC/src/vulkan/wsi/wsi_common.c" ]]; then
        rm "$SRC/src/vulkan/wsi/wsi_common.c"
    fi
    cp "$REF/src/vulkan/wsi/wsi_common.c" "$SRC/src/vulkan/wsi/"
    rm -f "$SRC/src/vulkan/wsi/alroot-gpu-buffer-v1-client-protocol.h" \
          "$SRC/src/vulkan/wsi/alroot-gpu-buffer-v1-protocol.c"
    (cd "$REF" && git apply --check "$PATCH")
    (cd "$REF" && git apply --check "$SYNC_PATCH")
    # The overlay lives underneath the Alroot Git checkout. git apply
    # silently SKIPS paths outside that enclosing Git worktree prefix;
    # use the cwd-relative POSIX patch utility in this disposable tree.
    (cd "$SRC" && patch --batch --forward -p1 -i "$PATCH")
    (cd "$SRC" && patch --batch --forward -p1 -i "$SYNC_PATCH")
    printf '%s\n' "$current_patch_hash" > "$WORK/.alroot-wsi-patch.sha256"
fi
rg -q 'ALROOT_WSI_AHB=1' "$SRC/src/vulkan/wsi/meson.build"
test -s "$SRC/src/vulkan/wsi/alroot-gpu-buffer-v1-protocol.c"
rg -q 'image_params->image_type != WSI_IMAGE_TYPE_ANDROID' "$SRC/src/vulkan/wsi/wsi_common.c"

# Meson is installed as a Python module on the device, not on PATH.
# Run it from an absolute shim in the private build folder; passing
# sys.argv[0]='meson' from repo root creates a broken Ninja path.
cat > "$WORK/meson" <<'PY'
#!/data/data/com.termux/files/usr/bin/python3
from mesonbuild.mesonmain import main
import sys
sys.exit(main())
PY
chmod 700 "$WORK/meson"

# Meson build options keep this test small. Unsupported Termux dependency
# discovery stops with a clear error, never triggers package installation.
if [[ ! -f "$BUILD/build.ninja" ]]; then
    mkdir -p "$BUILD"
    "$WORK/meson" setup "$BUILD" "$SRC" \
        --prefix="$WORK/prefix" \
        -Dvulkan-drivers=wrapper \
        -Dplatforms=x11,wayland \
        -Dgallium-drivers= \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled \
        -Dllvm=disabled -Dbuild-tests=false \
        -Dgallium-va=disabled \
        -Dvideo-codecs=
fi
if ! rg -q 'alroot-gpu-buffer-v1-protocol' "$BUILD/build.ninja"; then
    "$WORK/meson" setup --reconfigure "$BUILD" "$SRC"
fi
rg -q 'alroot-gpu-buffer-v1-protocol' "$BUILD/build.ninja"
# The availability attribute is evaluated from Clang's actual Android
# TARGET level, not from redefining __ANDROID_API__. Set only the private
# archive's compiler target (S26U Android 35). Never change the global
# Termux toolchain, CFLAGS or installed Android library ABI.
"$WORK/meson" configure "$BUILD" -Dc_args=--target=aarch64-linux-android35
ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-2}" src/vulkan/wsi/libvulkan_wsi.a
echo 'PASS: opt-in Mesa Vulkan wrapper Wayland/AHB WSI compiled privately'
if [[ "${ALROOT_BUILD_WRAPPER_FULL:-0}" == 1 ]]; then
    # Mesa's X11 WSI needs Termux's Bionic SysV-SHM shim at the final
    # private ICD link step. Do not modify Termux's global linker flags.
    "$WORK/meson" configure "$BUILD" \
        -Dc_link_args=-landroid-shmem -Dcpp_link_args=-landroid-shmem
    ninja -C "$BUILD" -j "${ALROOT_BUILD_JOBS:-2}" \
        src/vulkan/wrapper/libvulkan_wrapper.so
    # Never use the DLL at its Meson build path for a Vulkan device test:
    # its generated RUNPATH has $ORIGIN/../../android_stub, and that dummy
    # libnativewindow.so *silently hides every GPU*. The production
    # Termux library instead resolves Android's REAL libnativewindow.so.
    # Staging a copy in a sibling directory removes the stub RUNPATH.
    runtime="$WORK/../wrapper-private-runtime"
    mkdir -p "$runtime"
    cp "$BUILD/src/vulkan/wrapper/libvulkan_wrapper.so" \
       "$runtime/libvulkan_wrapper.so"
    python3 - "$runtime" <<'PY'
import json, pathlib, sys
runtime=pathlib.Path(sys.argv[1]).resolve()
manifest={
    "file_format_version":"1.0.1",
    "ICD":{
        "library_path":str(runtime/"libvulkan_wrapper.so"),
        "api_version":"1.3.359"
    }
}
(runtime/"wrapper_icd.private.json").write_text(json.dumps(manifest,indent=2)+"\n")
PY
    echo "PASS: private wrapper ICD staged (no stub) at $runtime/libvulkan_wrapper.so"
    if [[ "${ALROOT_VERIFY_WRAPPER:-0}" == 1 ]]; then
        VK_DRIVER_FILES="$runtime/wrapper_icd.private.json" \
            timeout 15 vulkaninfo --summary > "$runtime/vulkaninfo-summary.log" 2>&1 || {
                tail -35 "$runtime/vulkaninfo-summary.log" >&2
                exit 2
            }
        grep -q 'deviceName' "$runtime/vulkaninfo-summary.log" || {
            echo 'Private Vulkan wrapper did not expose a physical GPU' >&2
            exit 2
        }
        grep 'deviceName\|apiVersion\|driverID' "$runtime/vulkaninfo-summary.log" | tail -8
        echo 'PASS: private wrapper ICD enumerates a real GPU'
    fi
fi
echo 'NOT TESTED: full Wayland swapchain vkcube visible pixels'
