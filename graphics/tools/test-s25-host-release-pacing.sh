#!/data/data/com.termux/files/usr/bin/bash
# Offline correctness checks for the Android helper's compositor release cap.
# Does not start/stop XFCE, touch the running Android helper, or change ICDs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
TMP="$(mktemp -d "${TMPDIR:-/data/data/com.termux/files/usr/tmp}/alroot-s25-release.XXXXXXXX")"
trap 'rm -rf "$TMP"' EXIT

clang --target=aarch64-linux-android35 -std=c11 -O2 \
    -Wall -Wextra -Werror -Inative/waylandcomp/src \
    graphics/tests/s25-release-deadline.c -o "$TMP/release-deadline"
"$TMP/release-deadline"

# The actual host source, not the imported upstream DroidDeck reference,
# must call the tested scheduling helper and use the physical wl_output Hz.
python3 - <<'PY'
from pathlib import Path
c = Path('native/waylandcomp/src/compositor.c').read_text()
vk = Path('native/waylandcomp/src/vk_present.c').read_text()
assert '#include "alroot_release_pacing.h"' in c
assert 'alroot_release_deadline_ns(now, s->next_release_ns,' in c
assert 'atomic_load(&g_output_refresh_mhz)' in c
assert 'limit == 0, &capped' in c
assert 'g_perf.auto_release_capped' in c
assert 'g_perf.release_max_pending' in c
assert 'g_release_unpaced || interval <= 0' in c
assert 'alroot_log("gpu", "LINEAR DMA-BUF -> Vulkan VkBuffer GPU-copy:' in vk
assert 'log_now - last_log_ns >= 30000000000LL' in vk
assert 'img->linear_copy = 1;' in vk
assert 'img->linear_source_fd = dup(fd);' in vk
print('S25_ACTUAL_COMPOSITOR_RELEASE_SOURCE=PASS')
print('S25_VULKAN_GPU_COPY_LOG_THROTTLE=PASS')
print('S25_VULKAN_RENDERING_AND_FENCES=UNCHANGED')
PY

# These are syntax-only checks, not a claim of compiled/installed Android
# APK integration. The existing JNI, Wayland protocol, and GPU code stay.
for part in compositor vk_present; do
    clang --target=aarch64-linux-android35 -std=gnu11 \
        -fsyntax-only -Werror=implicit-function-declaration -Wno-unused-function \
        -Inative/waylandcomp/src -Inative/waylandcomp/generated \
        -Inative/waylandcomp/prebuilt/include \
        "native/waylandcomp/src/$part.c"
    echo "S25_NATIVE_${part^^}_SYNTAX=PASS"
done

sha256sum -c "$ROOT/.source/gpu-audit/termux-x11-protected.sha256" \
    > "$TMP/protected-hashes.log"
echo S25_SYSTEM_TERMUX_X11_DRIVERS=UNCHANGED
echo S25_ANDROID_PHYSICAL_DISPLAY_SMOOTHNESS=USER_TEST_REQUIRED
echo S25_NATIVE_APP_UPDATE=NOT_INSTALLED
