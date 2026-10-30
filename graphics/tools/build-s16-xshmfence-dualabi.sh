#!/data/data/com.termux/files/usr/bin/bash
# Build the Stage-S1.7 Bionic Xwayland-only (NOT global Termux / glibc)
# DRI3 xshmfence 96-byte pthread vs 4-byte glibc futex compatibility shim.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
DEST="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/xshmfence"
mkdir -p "$DEST"
SOURCE="$ROOT/graphics/compat/s16-xshmfence-dualabi.c"
[[ -f "$SOURCE" ]] || { echo 'S16_DUALABI_SOURCE_MISSING' >&2;exit 2; }
clang --target=aarch64-linux-android35 -std=gnu11 -O2 -fPIC -shared \
    -Wall -Wextra -Werror "$SOURCE" -ldl \
    -o "$DEST/libalroot_xshmfence_dualabi.so"
[[ -s "$DEST/libalroot_xshmfence_dualabi.so" ]]
for symbol in xshmfence_map_shm xshmfence_unmap_shm xshmfence_alloc_shm \
              xshmfence_trigger xshmfence_await xshmfence_query xshmfence_reset;do
    nm -D --defined-only "$DEST/libalroot_xshmfence_dualabi.so" |
        grep -Eq " [TW] ${symbol}$" || {
        echo "S16_DUALABI_MISSING_SYMBOL=$symbol" >&2;exit 4;
    }
done
printf 'S16_DUALABI_LIBRARY_BUILT=PASS\nS16_DUALABI_PATH=%s\n' "$DEST/libalroot_xshmfence_dualabi.so"
sha256sum "$DEST/libalroot_xshmfence_dualabi.so"
echo 'S16_GLOBAL_LIBXSHMFENCE=UNMODIFIED'

# Exercise BOTH shared-memory ABIs on real Bionic pthread/futex syscalls,
# including an actual forked waiter.  Timeout rejects silent deadlocks.
clang --target=aarch64-linux-android35 -std=gnu11 -O2 -Wall -Wextra -Werror \
    "$ROOT/graphics/tests/s16-xshmfence-dualabi-test.c" -lxshmfence \
    -o "$DEST/s16-xshmfence-dualabi-test"
for abi in bionic96 glibc4; do
    log="$DEST/s16-xshmfence-dualabi-test-${abi}.log"
    env S16_XSHMFENCE_TRACE=1 \
        LD_PRELOAD="$DEST/libalroot_xshmfence_dualabi.so${LD_PRELOAD:+ $LD_PRELOAD}" \
        timeout -s TERM -k 1s 8s \
        "$DEST/s16-xshmfence-dualabi-test" "$abi" > "$log" 2>&1 || {
        tail -22 "$log";echo "S16_DUALABI_${abi}_FENCE_TEST=FAIL";exit 6;
    }
    grep -Fxq "S16_DUALABI_${abi}_CROSS_PROCESS_WAKE=PASS" "$log"
    grep -Fxq "S16_DUALABI_${abi}_NATIVE_STATE_MACHINE=PASS" "$log"
    echo "S16_DUALABI_${abi}_FENCE_TEST=PASS"
done

# Unsupported shared-memory sizes are a hard error, never a Bionic mutex
# cast. This explicit negative control catches accidental ABI overmatching.
BAD="$DEST/s16-xshmfence-dualabi-test-invalid8.log"
env S16_XSHMFENCE_TRACE=1 \
    LD_PRELOAD="$DEST/libalroot_xshmfence_dualabi.so${LD_PRELOAD:+ $LD_PRELOAD}" \
    timeout -s TERM -k 1s 6s \
    "$DEST/s16-xshmfence-dualabi-test" invalid8 >"$BAD" 2>&1 || {
    tail -20 "$BAD";echo 'S16_DUALABI_INVALID8_NEGATIVE_TEST=FAIL';exit 7;
}
grep -Fxq 'S16_DUALABI_INVALID8_REJECTED=PASS' "$BAD"
echo 'S16_DUALABI_INVALID8_NEGATIVE_TEST=PASS'
