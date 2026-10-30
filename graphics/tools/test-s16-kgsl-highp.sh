#!/data/data/com.termux/files/usr/bin/bash
# Reproduce right-half-of-1920px corruption on real Adreno/KGSL EGL GLES.
# Independently validates source pixmap highp shader precision (not Android UI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
OUT="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/precision-half"
mkdir -p "$OUT"
clang --target=aarch64-linux-android35 -std=gnu11 -O2 -Wall -Wextra -Werror \
    graphics/tests/s16-glamor-precision-half.c -lEGL -lGLESv2 \
    -o "$OUT/glamor-mediump-vs-highp"
log="$OUT/highp-1920-actual-adreno-gles.log"
env MESA_LOADER_DRIVER_OVERRIDE=kgsl GALLIUM_DRIVER=freedreno \
    FD_KGSL_ENABLE_DMABUF=1 EGL_PLATFORM=surfaceless \
    timeout 15s "$OUT/glamor-mediump-vs-highp" >"$log" 2>&1 || {
    cat "$log";echo 'S16_KGSL_HIGHP_PROOF=FAIL';exit 3;
}
cat "$log"
grep -q '^S16_GLSL_MEDIUMP_TEXTURE_COPY_LEFT_BAD=0 RIGHT_BAD=[1-9][0-9]* ' "$log"
grep -q '^S16_GLSL_HIGHP_TEXTURE_COPY_LEFT_BAD=0 RIGHT_BAD=0 X_GE1024_BAD=0 ' "$log"
grep -q '^S16_GLSL_HIGHP_FIX_SUPPORTED=PASS_RIGHT_HALF_NO_BAD_PIXELS$' "$log"
grep -Fq '"precision highp float;\n"' \
    .source/gpu-stage1/xwayland-kgsl/source/glamor/glamor_priv.h
echo 'S16_KGSL_GLAMOR_PRIVATE_SOURCE_HIGHP=PASS'

# Compile XRender sample and exercise the *actual private* 24.1.14
# GLAMOR shader path, not just standalone native EGL GLES. This is the
# 1920px right-half acceptance that matches the user's XFCE screenshot.
clang --target=aarch64-linux-android35 -O2 -std=gnu11 -Wall -Wextra -Werror \
    graphics/tests/s16-glamor-xrender-wide.c -lXrender -lX11 \
    -o "$OUT/s16-glamor-xrender-wide"
S16_TEST_GEOMETRY=1920x1080 S16_XRENDER_WIDE_TEST=1 \
    python3 graphics/tools/test-s16-xwayland-rootful-lock.py \
    >"$OUT/xrender-1920-highp-acceptance.log" 2>&1 || {
    cat "$OUT/xrender-1920-highp-acceptance.log"
    echo 'S16_GLAMOR_HIGHP_XRENDER_1920=FAIL'
    exit 5
}
grep -q '^S16_XRENDER_GLAMOR_1920_LEFT_BAD=0 RIGHT_BAD=0 ' "$OUT/xrender-1920-highp-acceptance.log"
grep -q '^S16_ANDROID_HOST_VKBUFFER_GPU_COPY=LOG_CONFIRMED$' "$OUT/xrender-1920-highp-acceptance.log"
echo 'S16_GLAMOR_HIGHP_XRENDER_1920=PASS_LEFT_0_RIGHT_0'
echo 'S16_GLAMOR_HIGHP_ANDROID_HOST_VKBUFFER_GPU_COPY=PASS_HOST_LOG'

# Optional exact shader-precision A/B negative control when the on-device
# scratch retains the previously built mediump-only private Xwayland.
if [[ -x "$OUT/Xwayland-mediump-1920" ]]; then
    rc=0
    S16_PRIVATE_TEST_XWAYLAND_BINARY="$OUT/Xwayland-mediump-1920" \
    S16_TEST_GEOMETRY=1920x1080 S16_XRENDER_WIDE_TEST=1 \
        python3 graphics/tools/test-s16-xwayland-rootful-lock.py \
        >"$OUT/xrender-1920-mediump-negative-control.log" 2>&1 || rc=$?
    if [[ "$rc" != 7 ]] ||
       ! grep -Eq '^S16_XRENDER_GLAMOR_1920_LEFT_BAD=0 RIGHT_BAD=[1-9][0-9]* ' \
             "$OUT/xrender-1920-mediump-negative-control.log"; then
        echo "S16_GLES_MEDIUMP_NEGATIVE_CONTROL=FAIL_RC:$rc"
        tail -22 "$OUT/xrender-1920-mediump-negative-control.log"
        exit 6
    fi
    grep '^S16_XRENDER_GLAMOR_1920_' "$OUT/xrender-1920-mediump-negative-control.log"
    echo 'S16_GLES_MEDIUMP_NEGATIVE_CONTROL=PASS_RIGHT_HALF_BROKEN_AS_EXPECTED'
else
    echo 'S16_GLES_MEDIUMP_NEGATIVE_CONTROL=SKIPPED_NO_BASELINE_BINARY'
fi

echo 'S16_KGSL_RIGHT_HALF_PRECISION_REGRESSION=PASS'
echo 'S16_ANDROID_SCREEN_VISIBLE_TEXT=REQUIRES_HUMAN_VISUAL_A_B'
