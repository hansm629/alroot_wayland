package com.alroot.wayland;

/**
 * A software IME may synthesize A..Z KeyEvents with scanCode == 0 and
 * META_SHIFT_ON without ever emitting a corresponding Shift key event.
 * The native Wayland keyboard takes evdev codes, not Android metaState, so
 * those keys otherwise arrive as lowercase. Never alter hardware key chords
 * or the Fcitx5 setComposingText/commitText path.
 */
final class ImeVirtualShiftPolicy {
    private ImeVirtualShiftPolicy() {}

    static boolean needsScopedShift(boolean scanless, boolean alphabetKey,
            boolean uppercaseRequested, boolean guestShiftHeld,
            boolean shortcutModifiersHeld) {
        return scanless && alphabetKey && uppercaseRequested &&
                !guestShiftHeld && !shortcutModifiersHeld;
    }
}
