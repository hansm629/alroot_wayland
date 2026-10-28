package com.alroot.wayland;

public final class ImeVirtualShiftPolicyTest {
    private static void check(boolean want, boolean scanless, boolean letter,
            boolean uppercase, boolean shiftHeld, boolean chord) {
        boolean actual = ImeVirtualShiftPolicy.needsScopedShift(
                scanless, letter, uppercase, shiftHeld, chord);
        if (actual != want) throw new AssertionError(
                "Shift decision mismatch for scanless=" + scanless +
                " letter=" + letter + " upper=" + uppercase +
                " shiftHeld=" + shiftHeld + " chord=" + chord);
    }

    public static void main(String[] args) {
        check(true, true, true, true, false, false); // Samsung soft Shift+A
        check(false, true, true, false, false, false); // soft lowercase
        check(false, false, true, true, false, false); // physical keyboard
        check(false, true, false, true, false, false); // Hangul/symbol/DEL
        check(false, true, true, true, true, false); // actual Shift held
        check(false, true, true, true, false, true); // Ctrl/Alt/Meta
        System.out.println("PASS: uppercase virtual key is scoped; physical and Hangul preserved");
    }
}
