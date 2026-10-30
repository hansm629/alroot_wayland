#!/data/data/com.termux/files/usr/bin/bash
# Android-free pointer ownership / viewport regression plus S20 no-regression
# checks. These never inject input or restart the user's active XFCE session.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$root"

bash tests/p18-startup-regression.sh
bash graphics/tools/test-s20-pointer-atomic.sh

python3 - <<'PY'
from pathlib import Path
root = Path.cwd()
view = (root / 'app/src/main/java/com/alroot/wayland/InputSurfaceView.java').read_text()
overlay = (root / 'app/src/main/java/com/alroot/wayland/CursorOverlay.java').read_text()
activity = (root / 'app/src/main/java/com/alroot/wayland/MainActivity.java').read_text()
checks = {
    'startup_touchpad_owns_cursor': 'touchpadCursorActive = inputMode == MODE_TOUCHPAD;' in view,
    'native_cursor_bitmap_not_required_for_overlay': 'touchpadCursorActive, pointerViewSized);' in view,
    'hardware_cursor_hidden_during_fallback': 'guestCursorHidden, touchpadCursorActive))' in view,
    'overlay_has_temporary_arrow': 'canvas.drawPath(startupArrow, startupFill);' in overlay,
    'pip_native_pointer_position_not_warped': 'Never emit a synthetic Wayland motion for a resize.' in view,
    'pip_view_pointer_remapped': 'PointerStartupPolicy.remapViewCoordinate(lastX,' in view,
    'pip_overlay_redrawn_on_restore': 'if (cursorOverlay != null) cursorOverlay.invalidate();' in activity,
    's20_atomic_click_unchanged': 'WaylandCompositor.nativeSendSceneButton(gx, gy, evdev, down ? 1 : 0);' in view,
    's20_captured_relative_unchanged': 'Do NOT integrate captured deltas' in view,
    'no_density_magnification_of_startup_cursor':
        'PointerStartupPolicy.fallbackCursorScale(' in overlay,
}
for label, valid in checks.items():
    print('S21_' + label.upper() + '=' + ('PASS' if valid else 'FAIL'))
    if not valid:
        raise SystemExit(1)
print('S21_REAL_ANDROID_STARTUP_AND_PIP_VISUAL=TEST_PENDING')
PY
