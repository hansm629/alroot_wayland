package com.alroot.wayland;

/** Suppresses a DEL reported twice by one Android IME keypress.
 *
 * Some IMEs emit both sendKeyEvent(KEYCODE_DEL) and
 * deleteSurroundingText(1,0). Treat a tightly adjacent event from
 * the *other API* as the same operation, but never coalesce two
 * calls from the same source (a legitimate repeated Backspace).
 *
 * No text content, window focus, clipboard or native X11 state is read.
 */
public final class ImeBackspaceGuard {
    public static final int KEY_EVENT = 1;
    public static final int SURROUNDING_TEXT = 2;
    private int lastSource;
    private long lastMs = -1;

    public synchronized boolean isEcho(int source, long nowMs) {
        if (lastMs >= 0 && nowMs >= lastMs && nowMs - lastMs <= 55 &&
                lastSource != source) {
            return true;
        }
        lastSource = source;
        lastMs = nowMs;
        return false;
    }
    public synchronized void reset() {
        lastSource = 0;
        lastMs = -1;
    }
}
