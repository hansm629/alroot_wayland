package com.alroot.wayland;

/**
 * The cursor bitmap and X11 hit-test must use exactly the same inverse
 * Android View picture -> guest scene transform. No cached Vulkan output
 * mapping, second scaling, pointer acceleration or Android screen metrics.
 *
 * Pure Java and independently testable without an Android Activity.
 */
final class PointerSceneCoordinatePolicy {
    private PointerSceneCoordinatePolicy() {}

    static int guestCoordinate(float view, float pictureStart, float guestPerView,
                               int guestExtent) {
        if (guestExtent <= 1 || !Float.isFinite(view) ||
                !Float.isFinite(pictureStart) || !Float.isFinite(guestPerView) ||
                guestPerView <= 0f)
            return 0;
        float coord = (view - pictureStart) * guestPerView;
        // Never return guestExtent: X11 positions are 0..extent-1.
        return Math.max(0, Math.min(guestExtent - 1, Math.round(coord)));
    }
}
