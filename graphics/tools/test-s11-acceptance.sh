#!/data/data/com.termux/files/usr/bin/bash
# Run only after explicitly installing the signed GPU-S1.1 host APK.
# No automatic APK install, server restart, or global driver replacement.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
START="$(date +'%m-%d %H:%M:%S.000')"
OUT="$ROOT/.source/gpu-stage1/s11-acceptance"
mkdir -p "$OUT"

echo 'S1.1: offline XDG dimension contract'
bash graphics/tools/test-s11-geometry.sh | tail -2
echo 'S1.1: live windowed/fullscreen/maximized XDG handshake'
bash graphics/tools/test-s11-xdg-protocol.sh | tee "$OUT/xdg-configure.log"
echo 'S1.1: EGL resized-buffer release and reuse stress'
bash graphics/tools/test-s11-ahb-lifetime.sh egl 24 |
    tee "$OUT/egl-lifetime.log" |
    grep -E 'S11_|AHB_EGL_WINDOW_BUFFER_RELEASE_EVENTS'
echo 'S1.1: Vulkan wrapper resized-buffer release and reuse stress'
bash graphics/tools/test-s11-ahb-lifetime.sh vulkan 24 |
    tee "$OUT/vulkan-lifetime.log" |
    grep -E 'S11_|AHB_EGL_WINDOW_BUFFER_RELEASE_EVENTS'
echo 'S1.1: sustained FIFO / MAILBOX 360-frame Vulkan'
bash graphics/tools/test-vkcube-stage1.sh fifo 360 | tee "$OUT/fifo.log"
bash graphics/tools/test-vkcube-stage1.sh mailbox 360 | tee "$OUT/mailbox.log"
echo 'S1.1: protect original Termux-X11 DRI3 and ICDs'
bash graphics/tools/snapshot-termux-x11.sh verify | tail -3

echo 'S1.1: sample AHB lifetime metrics after the next host stats tick'
sleep 11
logcat -d -t "$START" -s AlrootWayland > "$OUT/host.log" 2>&1 || :
stats="$(grep '\[gpu-lifetime\]' "$OUT/host.log" | tail -1 || :)"
if [[ -n "$stats" ]]; then
    echo "$stats"
else
    echo 'WARNING: new AHB lifetime metrics absent; confirm GPU-S1.1 APK is installed'
fi
if grep -E '\[error\].*(import failed|buffer accounting underflow|Vulkan device lost)' "$OUT/host.log"; then
    echo 'FAIL: host GPU import/accounting error detected' >&2
    exit 1
fi
echo 'PASS: available S1.1 tests completed; on-device pixel/resize inspection still required'
echo "Logs: $OUT"
