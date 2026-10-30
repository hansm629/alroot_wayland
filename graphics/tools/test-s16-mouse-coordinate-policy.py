#!/data/data/com.termux/files/usr/bin/python3
"""Offline policy regression: Alroot hardware pointer event handoff and FIT mapping.

Structural assertions cover native click-before-button coordinate synchronization
and duplicate suppression. Mathematical checks cover the Android output-space
mapping used by InputSurfaceView and VkPresent, not a real human click.
"""
from pathlib import Path
import math

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
    'native_scene_input_uses_inverse_output_map':
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
        # Android: round(viewX / viewWidth * 1920) & equivalent y
        inp_x = round(view_x / ow * 1920)
        inp_y = round(view_y / oh * 1080)
        # C: output_to_scene(inputX * nativeOutputW / 1920, ...)
        sx = ((inp_x * ow / 1920) - px0) / scale
        sy = ((inp_y * oh / 1080) - py0) / scale
        max_error = max(abs(sx - ux * 1920), abs(sy - uy * 1080))
        assert max_error < 2.8, (ow, oh, ux, uy, max_error)
    print(f'S16_POINTER_FIT_COORDINATE_INVERSE[{ow}x{oh}]=PASS')
print('S16_POINTER_POLICY_TEST=PASS_OFFLINE_NOT_PHYSICAL_CLICK')
