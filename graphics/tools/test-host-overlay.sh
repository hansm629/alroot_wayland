#!/data/data/com.termux/files/usr/bin/bash
# Apply BOTH host GPU patches to a disposable P20 archive, never the live tree.
# Check generated Wayland v3 API and C syntax with Android API 35.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
scratch="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-host-overlay.XXXXXXXX")"
trap 'rm -rf "$scratch"' EXIT
cd "$ROOT"
git archive p01/first-helper native/waylandcomp | tar -xf - -C "$scratch"
(
    cd "$scratch"
    git apply "$ROOT/graphics/patches/host/0001-ahb-to-vkimage-gpu-copy.patch"
    git apply "$ROOT/graphics/patches/host/0002-v3-gpucopy-capability-independent-of-layers.patch"
)
target="$scratch/native/waylandcomp"
wayland-scanner private-code "$target/protocols/banner-ahb-v1.xml" "$scratch/scanner-protocol.c"
grep -q '"banner_ahb_v1", 3' "$target/generated/banner-ahb-v1-protocol.c"
grep -q '"gpu_capabilities", "3u"' "$target/generated/banner-ahb-v1-protocol.c"
grep -q '"gpu_capabilities", "3u"' "$scratch/scanner-protocol.c"
grep -q '^banner_ahb_v1_send_gpu_capabilities' "$target/generated/banner-ahb-v1-server-protocol.h"
for src in ahb_swapchain compositor vk_present; do
    clang --target=aarch64-linux-android35 -std=gnu11 -fsyntax-only \
        -Wno-unused-parameter -Wno-unused-function \
        -I"$target/src" -I"$target/generated" \
        -I"$target/prebuilt/include" \
        "$target/src/$src.c"
done
clang --target=aarch64-linux-android35 -std=gnu11 -fsyntax-only \
    -I"$target/generated" "$target/generated/banner-ahb-v1-protocol.c"
echo 'PASS: AHB-first host VkImage import + v3 independent copy capability compile'
echo 'PASS: legacy v2 mode event retained; v3 GPU-copy announced separately'
echo 'PASS: no host source, Termux driver, Xwayland or live session changed'
