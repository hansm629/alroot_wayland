#!/data/data/com.termux/files/usr/bin/bash
# Fully isolated protocol-v3 negotiation and v2 compatibility test.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
cd "$ROOT"
temp="$(mktemp -d "${TMPDIR:-$PREFIX/tmp}/alroot-ahb-protocol.XXXXXXXX")"
chmod 700 "$temp"
server_pid=""
cleanup() {
    [[ -z "$server_pid" ]] || { kill "$server_pid" 2>/dev/null || :; wait "$server_pid" 2>/dev/null || :; }
    rm -rf "$temp"
}
trap cleanup EXIT
xml=graphics/protocols/banner-ahb-v3.xml
wayland-scanner server-header "$xml" "$temp/banner-ahb-v1-server-protocol.h"
wayland-scanner client-header "$xml" "$temp/banner-ahb-v1-client-protocol.h"
wayland-scanner private-code "$xml" "$temp/banner-ahb-v1-protocol.c"
clang -std=c11 -Wall -Wextra -Werror -I"$temp" \
    graphics/tests/mock-ahb-wsi-server.c "$temp/banner-ahb-v1-protocol.c" \
    -o "$temp/server" -lwayland-server
clang -std=c11 -Wall -Wextra -Werror -I"$temp" \
    graphics/tools/probe-ahb-wsi.c "$temp/banner-ahb-v1-protocol.c" \
    -o "$temp/client" -lwayland-client
export XDG_RUNTIME_DIR="$temp" WAYLAND_DISPLAY=stage1-test
unset DISPLAY
for version in 2 3; do
    "$temp/server" stage1-test "$version" 1 > "$temp/server.log" 2>&1 &
    server_pid=$!
    ready=0
    for _ in $(seq 1 60); do
        [[ ! -S "$temp/stage1-test" ]] || { ready=1; break; }
        sleep .04
    done
    [[ "$ready" == 1 ]] || { cat "$temp/server.log"; exit 2; }
    "$temp/client" > "$temp/client-$version.log"
    grep -q "^ALROOT_AHB_PROTOCOL_VERSION=$version$" "$temp/client-$version.log"
    grep -q '^ALROOT_DIRECT_LAYER_MODE=OFF$' "$temp/client-$version.log"
    if [[ "$version" == 2 ]]; then
        grep -q '^ALROOT_GPU_COPY_CAPABILITY_EVENT=0$' "$temp/client-$version.log"
        grep -q '^ALROOT_AHB_VKIMAGE_GPU_COPY_CANDIDATE=0$' "$temp/client-$version.log"
    else
        grep -q '^ALROOT_GPU_COPY_CAPABILITY_EVENT=1$' "$temp/client-$version.log"
        grep -q '^ALROOT_AHB_VKIMAGE_GPU_COPY_CANDIDATE=1$' "$temp/client-$version.log"
        grep -q '^ALROOT_DIRECT_LAYER_CAPABILITY=0$' "$temp/client-$version.log"
    fi
    echo "PASS: banner_ahb_v1 v$version negotiated independently of direct layer"
    kill "$server_pid" 2>/dev/null || :
    wait "$server_pid" 2>/dev/null || :
    server_pid=""
    rm -f "$temp/stage1-test" "$temp/stage1-test.lock"
done
