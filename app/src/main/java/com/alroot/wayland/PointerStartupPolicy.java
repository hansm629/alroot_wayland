package com.alroot.wayland;

/**
 * In touchpad mode Android's uncaptured system cursor has an unrelated
 * coordinate (often 0,0 on first Activity launch). The guest seat is already
 * centered. The Android hardware and project software cursors must never
 * both own the pointer during startup or PiP restoration.
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
        // Hide Android's pointer whenever the touchpad overlay owns the
        // pointer, including while its first guest bitmap is still pending.
        // The overlay supplies a small fallback arrow until the bitmap arrives.
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

    /** Preserve a logical guest position across PiP/fullscreen view resizing. */
    static float remapViewCoordinate(float coordinate, float oldStart, float oldSize,
            float newStart, float newSize) {
        if (!Float.isFinite(coordinate) || !Float.isFinite(oldStart) ||
                !Float.isFinite(newStart) || !Float.isFinite(oldSize) ||
                !Float.isFinite(newSize) || oldSize <= 0f || newSize <= 0f)
            return midpoint(newStart, newStart + Math.max(0f, newSize));
        float relative = (coordinate - oldStart) / oldSize;
        return Math.max(newStart, Math.min(newStart + Math.max(0f, newSize - 1f),
                newStart + relative * newSize));
    }

    /** The P18 density-multiplied fallback arrow was up to ~2x too large. */
    static float fallbackCursorScale(float sceneToView) {
        if (!Float.isFinite(sceneToView) || sceneToView <= 0f) return 1f;
        return Math.min(1f, sceneToView);
    }
}
