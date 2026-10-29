#!/data/data/com.termux/files/usr/bin/bash
# Scopes Vulkan loader and ICD to this test process only.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
TMP="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-client-ahb.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
clang --target=aarch64-linux-android35 -std=gnu11 -Wall -Wextra -Werror \
    -Igraphics/include graphics/tests/ahb-vulkan-cross-process.c -ldl -o "$TMP/test"
case "${1:-wrapper}" in
wrapper)
    export VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/wrapper_icd.aarch64.json"
    ;;
turnip)
    export VK_DRIVER_FILES="$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json"
    ;;
*)
    echo 'usage: test-vulkan-client-ahb.sh wrapper|turnip' >&2; exit 2
    ;;
esac
export ALROOT_TEST_VULKAN_LOADER="libvulkan.so.1"
export ALROOT_TEST_GPU_CLEAR=1
"$TMP/test"
