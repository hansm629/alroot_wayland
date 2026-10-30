#!/data/data/com.termux/files/usr/bin/python3
"""Offline policy regression: Alroot hardware pointer event handoff and FIT mapping.

Structural assertions cover native click-before-button coordinate synchronization
and duplicate suppression. Mathematical checks cover the Android output-space
mapping used by InputSurfaceView and VkPresent, not a real human click.
"""
from pathlib import Path
import math
import random

ROOT = Path(__file__).resolve().parents[2]
JAVA = (ROOT / 'app/src/main/java/com/alroot/wayland/InputSurfaceView.java').read_text()
CPP = (ROOT / 'native/waylandcomp/src/compositor.c').read_text()
VK = (ROOT / 'native/waylandcomp/src/vk_present.c').read_text()

checks = {
    'hardware_mouse_is_not_touchpad_gesture':
        'if (event.isFromSource(android.view.InputDevice.SOURCE_MOUSE))' in JAVA,
    'mouse_buttons_are_deduplicated':
        'if (((mouseButtonMask & bit) != 0) == down) return;' in JAVA,
    'generic_mouse_click_updates_position_before_button':
        'absolute(event.getX(), event.getY());\n                sendHardwareMouseButton' in JAVA,
    'touch_mouse_click_updates_position_before_button':
        'absolute(event.getX(), event.getY());\n                if (a == MotionEvent.ACTION_DOWN' in JAVA,
    'captured_mouse_uses_relative_motion_not_absolute':
        'Captured pointer events provide relative deltas only' in JAVA,
    'touchpad_cursor_is_authoritative_absolute':
        'if (inputMode == MODE_TOUCHPAD)' in JAVA and
        'absolute(lastX, lastY);' in JAVA,
    'captured_game_pointer_retains_relative_deltas':
        'WaylandCompositor.nativeSendSceneInput(6,' in JAVA and
        'Do NOT integrate captured deltas' in JAVA,
    'touchpad_tap_atomic_guest_coordinate_before_button':
        'WaylandCompositor.nativeSendSceneButton(gx, gy, evdev, down ? 1 : 0);' in JAVA and
        'case 13: {' in CPP and
        'pointer_event(x, y, (uint32_t)m.p3, m.p4 ? 1 : 0);' in CPP,
    'new_absolute_cursor_uses_guest_scene_no_second_transform':
        'WaylandCompositor.nativeSendSceneInput(12, gx, gy);' in JAVA and
        'PointerSceneCoordinatePolicy.guestCoordinate(' in JAVA and
        'case 12: {' in CPP and
        'pointer_event(x, y, 0, 0);' in CPP,
    'legacy_scene_input_inverse_kept_for_older_clients':
        'vkp_output_to_scene((double)px * ow / INPUT_SPACE_W' in CPP,
    'native_scene_fit_includes_output_offsets':
        '*sx = (ox - g_map.off_x) / g_map.kx' in VK and
        '*sy = (oy - g_map.off_y) / g_map.ky' in VK,
}
for name, ok in checks.items():
    print('S16_POINTER_' + name.upper() + '=' + ('PASS' if ok else 'FAIL'))
    if not ok:
        raise SystemExit(1)

# Reproduces on-device logs: 1920x1080 scene on 3120x1440 actual output,
# FIT maps to 2560x1440 with approximately 280px horizontal margins.
# Includes an isolated 752x423 PiP output and 1920x1080 desktop view.
for ow, oh in ((3120, 1440), (1920, 1080), (752, 423), (2560, 1440)):
    scale = min(ow / 1920, oh / 1080)
    px0 = (ow - 1920 * scale) / 2
    py0 = (oh - 1080 * scale) / 2
    for ux, uy in ((0.1, 0.2), (0.5, 0.5), (0.9, 0.8)):
        view_x = px0 + ux * 1920 * scale
        view_y = py0 + uy * 1080 * scale
        # Android type-12: one visual-picture-to-guest inverse, no additional
        # native Vulkan map (which may still reflect the previous frame).
        sx = round((view_x - px0) / scale)
        sy = round((view_y - py0) / scale)
        max_error = max(abs(sx - ux * 1920), abs(sy - uy * 1080))
        assert max_error < 2.8, (ow, oh, ux, uy, max_error)
    print(f'S16_POINTER_FIT_COORDINATE_INVERSE[{ow}x{oh}]=PASS')
# Long-run relative-gesture profile with the new absolute synchronization.
# The old path separately accumulated Java pointer coordinates and native
# relative input, allowing drift after clamping and resize. Every gesture
# now maps the displayed position directly back to the X11 logical scene.
# This simulates gesture jitter, long drags, hitting edges and retracing.
random.seed(629)
for ow, oh in ((3120, 1440), (1920, 1080), (752, 423)):
    scale = min(ow / 1920, oh / 1080)
    offset_x = (ow - 1920 * scale) / 2
    offset_y = (oh - 1080 * scale) / 2
    picture_width = 1920 * scale
    picture_height = 1080 * scale
    x = offset_x + picture_width / 2
    y = offset_y + picture_height / 2
    max_error = 0.0
    for step in range(50000):
        dx = random.gauss(0, 9)
        dy = random.gauss(0, 9)
        if step % 7000 < 40:
            dx += -70 if step % 14000 < 7000 else 70
            dy += 55 if step % 14000 < 7000 else -55
        x = max(offset_x, min(offset_x + picture_width - 1, x + dx))
        y = max(offset_y, min(offset_y + picture_height - 1, y + dy))
        guest_x = round((x - offset_x) / scale)
        guest_y = round((y - offset_y) / scale)
        expected_x = (x - offset_x) / scale
        expected_y = (y - offset_y) / scale
        err = max(abs(guest_x - expected_x), abs(guest_y - expected_y))
        max_error = max(err, max_error)
        assert err < 1.5, (ow, oh, step, err)
    print(f'S16_POINTER_LONG_GESTURE_50000_STEPS[{ow}x{oh}]=PASS max_error={max_error:.3f}guestpx')
print('S16_POINTER_POLICY_TEST=PASS_OFFLINE_NOT_PHYSICAL_CLICK')
