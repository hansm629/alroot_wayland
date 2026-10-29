#!/data/data/com.termux/files/usr/bin/bash
# Vendor-neutral, opt-in Vulkan wrapper WSI patch validation.
# Applies to a tiny disposable source overlay only: NEVER patches the
# reference repo, installed Termux ICD, or Termux-X11 DRI3 WSI.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
SRC="${ALROOT_WRAPPER_SOURCE:-$HOME/alroot-vk-lab/vulkan-wrapper-android}"
PATCH="$ROOT/graphics/patches/mesa/wrapper/0001-alroot-ahb-wayland-wsi-vkimage.patch"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-wrapper-wsi.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
mkdir -p "$TMP/src/vulkan/wsi"
cp "$SRC/src/vulkan/wsi/meson.build" "$TMP/src/vulkan/wsi/"
cp "$SRC/src/vulkan/wsi/wsi_common_wayland.c" "$TMP/src/vulkan/wsi/"
(cd "$SRC" && git apply --check "$PATCH")
(cd "$TMP" && git apply "$PATCH")

WSI="$TMP/src/vulkan/wsi"
rg -q 'WSI_WL_BUFFER_ALROOT_AHB' "$WSI/wsi_common_wayland.c"
rg -q 'VK_FORMAT_R8G8B8A8_UNORM' "$WSI/wsi_common_wayland.c"
rg -q 'Put the supported AHB format first' "$WSI/wsi_common_wayland.c"
rg -q 'AHardwareBuffer_sendHandleToUnixSocket' "$WSI/wsi_common_wayland.c"
rg -q 'ALROOT_WSI_AHB=1' "$WSI/meson.build"
rg -q 'with_wrapper_vk' "$WSI/meson.build"
rg -q 'VkResult complete = chain->base.wsi->WaitForFences' "$WSI/wsi_common_wayland.c"
test ! -e "$TMP/src/vulkan/wsi/wsi_common_x11.c"

# Protocol generated in the project must agree with the wrapper patch.
wayland-scanner client-header \
    native/waylandcomp/protocols/alroot-gpu-buffer-v1.xml \
    "$TMP/scanned-client.h"
wayland-scanner private-code \
    native/waylandcomp/protocols/alroot-gpu-buffer-v1.xml \
    "$TMP/scanned-protocol.c"
cmp "$TMP/scanned-client.h" "$WSI/alroot-gpu-buffer-v1-client-protocol.h"
# Project's tracked generated C omits one scanner-produced blank EOF line.
# Compare protocol contents, not trailing empty-line formatting.
python3 - "$TMP/scanned-protocol.c" "$WSI/alroot-gpu-buffer-v1-protocol.c" <<'PY'
from pathlib import Path
import sys
assert Path(sys.argv[1]).read_text().rstrip() == Path(sys.argv[2]).read_text().rstrip(), (
    "Generated client protocol differs from host")
PY
clang --target=aarch64-linux-android35 -std=c11 -Wall -Wextra -Werror \
    -fsyntax-only "$WSI/alroot-gpu-buffer-v1-protocol.c"
echo 'PASS: opt-in wrapper Wayland AHB image WSI patch applies'
echo 'PASS: public-NDK AHB protocol matches host, Wayland only (X11 untouched)'
echo 'PASS: wrapper-only Meson guard + scoped Stage-1 wait-fence present'
echo 'NOTE: entire Mesa wrapper WSI C compilation and visible present remain TEST-PENDING'
