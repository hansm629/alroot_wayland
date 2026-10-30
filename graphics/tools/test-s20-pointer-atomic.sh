#!/data/data/com.termux/files/usr/bin/bash
# Stage-S20 pointer drift regression: Java view mapping, JNI/C native atomic
# scene click routing, no raw relative stream for uncaptured rootful Xwayland,
# and existing IME/touchpad/capture isolation. No user input is injected.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
OUT="$ROOT/.source/gpu-stage27-pr25/s16-xwayland-gpu/s20-pointer"
mkdir -p "$OUT/classes"
cd "$ROOT"
"$PREFIX/bin/javac" -d "$OUT/classes" \
    app/src/main/java/com/alroot/wayland/PointerSceneCoordinatePolicy.java \
    graphics/tests/PointerSceneCoordinatePolicyTest.java
"$PREFIX/bin/java" -cp "$OUT/classes" \
    com.alroot.wayland.PointerSceneCoordinatePolicyTest
python3 graphics/tools/test-s16-mouse-coordinate-policy.py |tail -12
for part in compositor waylandcomp_jni;do
    clang --target=aarch64-linux-android35 -std=gnu11 -fsyntax-only \
          -Wno-unused-parameter -Wno-unused-function \
          -I native/waylandcomp/src -I native/waylandcomp/generated \
          -I native/waylandcomp/prebuilt/include \
          "native/waylandcomp/src/$part.c"
    echo "S20_NATIVE_${part^^}_SYNTAX=PASS"
done
python3 - <<'PY'
from pathlib import Path
root=Path.cwd()
cpp=(root/'native/waylandcomp/src/compositor.c').read_text()
java=(root/'app/src/main/java/com/alroot/wayland/InputSurfaceView.java').read_text()
jni=(root/'native/waylandcomp/src/waylandcomp_jni.c').read_text()
assert 'WaylandCompositor.nativeSendSceneButton(gx, gy, evdev, down ? 1 : 0);' in java
assert 'Java_com_alroot_wayland_WaylandCompositor_nativeSendSceneButton' in jni
assert 'struct input_msg m = { 13, x, y, button, !!pressed };' in cpp
assert 'case 13: {' in cpp
case=cpp[cpp.index('case 13: {'):cpp.index('case 8: output_resized(); break;')]
assert 'pointer_event(x, y, (uint32_t)m.p3, m.p4 ? 1 : 0);' in case
assert 'if (relative || k || target != g_desktop)' in cpp
assert 'g_raw_x = g_ptr_x; g_raw_y = g_ptr_y; g_raw_valid = 1;' in cpp
assert 'if (captured) {' in java and 'Do NOT integrate captured deltas' in java
print('S20_ATOMIC_BUTTON_AND_ROOTFUL_ABSOLUTE_ONLY_CONTRACT=PASS')
PY

echo 'S20_INPUT_PIPELINE_SOURCE_INTEGRITY=PASS'
echo 'S20_PHYSICAL_MOUSE_TOUCHPAD_DESKTOP=REQUIRES_UPDATED_APK_VISUAL_TEST'
