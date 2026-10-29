#!/data/data/com.termux/files/usr/bin/bash
# No global packages, APK, :0 / :90, renderer, or driver files are modified.
set -euo pipefail
cd "$(dirname "$(dirname "$(dirname "$(realpath "$0")")")")"
out="$(mktemp -d "${TMPDIR:-/data/data/com.termux/files/usr/tmp}/alroot-gpu-caps.XXXXXXXX")"
trap 'rm -rf "$out"' EXIT
clang -std=c11 -Wall -Wextra -Werror -Igraphics/include \
    graphics/src/capability_policy.c graphics/tests/test_capability_policy.c \
    -o "$out/policy"
"$out/policy"
git apply --check graphics/patches/host/0001-ahb-to-vkimage-gpu-copy.patch
echo 'PASS: vendor-neutral capability policy and isolated host patch dry-run'
