#!/data/data/com.termux/files/usr/bin/bash
# Never modifies a client, host, or swapchain. Read-only Wayland global check.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
temp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-ahb-probe.XXXXXXXX")"
trap 'rm -rf "$temp"' EXIT
xml="graphics/protocols/banner-ahb-v3.xml"
# Stage-1 protocol overlay is optional. If the host v3 overlay is applied in
# a private checkout, use that XML; P20 correctly advertises only v2.
if [[ "${ALROOT_GPU_V3_XML:-}" != "" ]]; then
    xml="$ALROOT_GPU_V3_XML"
fi
wayland-scanner client-header "$xml" "$temp/banner-ahb-v1-client-protocol.h"
wayland-scanner private-code "$xml" "$temp/banner-ahb-v1-protocol.c"
clang -std=c11 -Wall -Wextra -Werror -I"$temp" \
    graphics/tools/probe-ahb-wsi.c "$temp/banner-ahb-v1-protocol.c" \
    -o "$temp/probe" -lwayland-client
"$temp/probe"
