package com.alroot.wayland;

public final class PointerStartupPolicyTest {
    static void test(boolean actual, boolean expected, String message) {
        if (actual != expected) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        test(PointerStartupPolicy.hideSystemCursor(0, false, false, true), true,
                "Touchpad must not expose Android's 0,0 cursor");
        test(PointerStartupPolicy.showSoftwareCursor(0, false, true, false), false,
                "No custom cursor at uninitialized 0,0");
        test(PointerStartupPolicy.showSoftwareCursor(0, false, false, true), false,
                "Actual hardware mouse must retain the only visible cursor");
        test(PointerStartupPolicy.showSoftwareCursor(0, false, true, true), true,
                "Touchpad needs a cursor from startup, before any gesture or bitmap");
        test(PointerStartupPolicy.hideSystemCursor(0, false, false, true), true,
                "The centered fallback must hide Android's cursor even before a bitmap");
        test(PointerStartupPolicy.hideSystemCursor(0, false, false, false), false,
                "Physical mouse keeps one hardware pointer");
        test(PointerStartupPolicy.hideSystemCursor(2, false, false, false), false,
                "Direct touch retains its original system-pointer policy");
        test(PointerStartupPolicy.hideSystemCursor(2, true, false, false), true,
                "Captured mode hides Android system cursor");
        test(PointerStartupPolicy.showSoftwareCursor(2, true, false, true), true,
                "Captured mode draws its guest pointer");
        test(PointerStartupPolicy.showSoftwareCursor(0, false, true, true), true,
                "Touchpad displays the cursor through the Android guest overlay");
        if (PointerStartupPolicy.midpoint(0, 1920) != 960f ||
                PointerStartupPolicy.midpoint(0, 1080) != 540f ||
                PointerStartupPolicy.midpoint(120, 1320) != 720f)
            throw new AssertionError("Initial pointer must use the displayed view center");
        if (PointerStartupPolicy.fallbackCursorScale(2.2f) != 1f ||
                PointerStartupPolicy.fallbackCursorScale(.75f) != .75f ||
                PointerStartupPolicy.fallbackCursorScale(0f) != 1f)
            throw new AssertionError("P18 density scale must never enlarge temporary arrow");
        testViewResizes();
        System.out.println("PASS: startup cursor centered, Android duplicate hidden");
    }

    private static void close(float actual, float expected, String label) {
        if (Math.abs(actual - expected) > .1f)
            throw new AssertionError(label + " expected " + expected + ", got " + actual);
    }

    private static void testViewResizes() {
        // S26U immersive landscape FIT: 1920x1080 guest in 3120x1440
        // (280px side margins). Enter Samsung PiP 752x423 and return.
        float fullLeft = 280f, fullWidth = 2560f;
        float fullTop = 0f, fullHeight = 1440f;
        float pipWidth = 752f, pipHeight = 423f;
        for (float xPart : new float[] {.1f, .5f, .73f, .9f}) {
            for (float yPart : new float[] {.1f, .5f, .61f, .9f}) {
                float x = fullLeft + fullWidth * xPart;
                float y = fullTop + fullHeight * yPart;
                float initialX = x, initialY = y;
                for (int cycle = 0; cycle < 30; cycle++) {
                    x = PointerStartupPolicy.remapViewCoordinate(
                            x, fullLeft, fullWidth, 0f, pipWidth);
                    y = PointerStartupPolicy.remapViewCoordinate(
                            y, fullTop, fullHeight, 0f, pipHeight);
                    close(x / pipWidth, xPart, "PiP normalized X");
                    close(y / pipHeight, yPart, "PiP normalized Y");
                    x = PointerStartupPolicy.remapViewCoordinate(
                            x, 0f, pipWidth, fullLeft, fullWidth);
                    y = PointerStartupPolicy.remapViewCoordinate(
                            y, 0f, pipHeight, fullTop, fullHeight);
                    close(x, initialX, "PiP->fullscreen X cycle " + cycle);
                    close(y, initialY, "PiP->fullscreen Y cycle " + cycle);
                }
            }
        }
        close(PointerStartupPolicy.remapViewCoordinate(
                960f, 0f, 1920f, 0f, 752f), 376f, "center in PiP");
        close(PointerStartupPolicy.remapViewCoordinate(
                376f, 0f, 752f, 0f, 1920f), 960f, "center restored");
        close(PointerStartupPolicy.remapViewCoordinate(
                0f, 280f, 2560f, 0f, 752f), 0f, "left clamped");
        close(PointerStartupPolicy.remapViewCoordinate(
                2840f, 280f, 2560f, 0f, 752f), 751f, "right clamped");
        close(PointerStartupPolicy.remapViewCoordinate(
                Float.NaN, 0f, 0f, 0f, 752f), 376f, "invalid old viewport");
        System.out.println("PASS: PiP and full-screen pointer coordinates preserved for 480 cycles");
    }
}
