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
        // Hide the system pointer until the user actually moves a physical
        // mouse. After a real mouse event, retain the pre-P18 fast Android
        // hardware cursor path instead of forcing all DeX mouse motion
        // through a software overlay.
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
