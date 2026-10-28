package com.alroot.wayland;

/**
 * In touchpad mode Android's uncaptured system cursor has an unrelated
 * coordinate (often 0,0 on first Activity launch). The guest seat is already
 * centered. Never show both cursors while the Wayland desktop is starting.
 *
 * The direct-touch / virtual-touchscreen modes keep their original Android
 * cursor policy. This class has no Android dependencies for JVM regression.
 */
final class PointerStartupPolicy {
    static final int TOUCHPAD = 0;

    private PointerStartupPolicy() {}

    static boolean hideSystemCursor(int mode, boolean captured, boolean guestHidden,
            boolean touchpadOverlayActive) {
        // Our native Wayland compositor publishes ROLE_CURSOR as an Android
        // bitmap snapshot, but DOES NOT add it to render_scene(). Thus
        // Labwc's cursor is NOT drawn directly into the desktop framebuffer.
        // Only hide Android's mouse pointer when the project overlay really
        // owns a guest bitmap or when pointer capture is enabled.
        return captured || guestHidden ||
                (mode == TOUCHPAD && touchpadOverlayActive);
    }

    static boolean showSoftwareCursor(int mode, boolean captured,
            boolean touchpadCursorActive, boolean surfaceSized) {
        return surfaceSized && (captured ||
                (mode == TOUCHPAD && touchpadCursorActive));
    }

    static float midpoint(float start, float end) {
        return start + (end - start) / 2f;
    }

    /** The P18 density-multiplied fallback arrow was up to ~2x too large. */
    static float fallbackCursorScale(float sceneToView) {
        if (!Float.isFinite(sceneToView) || sceneToView <= 0f) return 1f;
        return Math.min(1f, sceneToView);
    }
}
