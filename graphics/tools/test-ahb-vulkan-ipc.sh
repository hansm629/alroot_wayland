#!/data/data/com.termux/files/usr/bin/bash
# Cross-process Android AHB -> host Vulkan VkImage, without a Wayland surface.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
tmp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-ahb-vulkan.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
    -Igraphics/include graphics/tests/ahb-vulkan-cross-process.c -ldl -o "$tmp/test"
"$tmp/test"
