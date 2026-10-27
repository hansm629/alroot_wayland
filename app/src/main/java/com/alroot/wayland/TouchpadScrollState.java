package com.alroot.wayland;

/** Android-independent natural two-finger scroll accumulator. */
public final class TouchpadScrollState {
    private final float activationSlop;
    private final float pixelsPer120;
    private float residual;
    private boolean scrolling;

    public TouchpadScrollState(float activationSlop, float density) {
        this.activationSlop = Math.max(0, activationSlop);
        this.pixelsPer120 = Math.max(1f, 32f * density);
    }

    public void reset() {
        residual = 0f;
        scrolling = false;
    }

    public boolean isScrolling() { return scrolling; }

    /** Positive finger delta moves down; negative Wayland axis scrolls up. */
    public int move(float fingerDeltaY) {
        residual += fingerDeltaY;
        if (!scrolling && Math.abs(residual) >= activationSlop) {
            scrolling = true;
            residual -= Math.signum(residual) * activationSlop;
        }
        if (!scrolling) return 0;
        int units = (int)(residual * 120f / pixelsPer120);
        if (units != 0) residual -= units * pixelsPer120 / 120f;
        return -units;
    }
}
