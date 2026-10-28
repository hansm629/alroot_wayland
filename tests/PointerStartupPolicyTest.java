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
                "No second software cursor beside the initial hardware pointer");
        test(PointerStartupPolicy.showSoftwareCursor(0, false, true, true), true,
                "Touchpad gesture may show its single guest cursor bitmap");
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
        System.out.println("PASS: startup cursor centered, Android duplicate hidden");
    }
}
