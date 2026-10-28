package com.alroot.wayland;

/**
 * Prevents a transient empty preedit frame during Korean Backspace edits.
 *
 * The Samsung IME may report deleteSurroundingText(1,0) immediately before
 * setComposingText("하") as it removes the final consonant from "한".
 * Sending native C("") then C("하") can make GTK draw an empty edit field
 * between those two events. We hold ONLY the empty deletion revision for
 * one short frame, so a following composing update replaces it directly.
 *
 * The caller owns the timer; this class is Android-free for JVM testing.
 */
public final class ImeCompositionSmoother {
    public interface Output {
        void composing(String text);
    }
    private boolean emptyDeletionPending;

    /** Return true only when an empty composing deletion was postponed. */
    public synchronized boolean deletedRevision(String text, Output output) {
        if (text == null) text = "";
        if (text.isEmpty()) {
            emptyDeletionPending = true;
            return true;
        }
        emptyDeletionPending = false;
        output.composing(text);
        return false;
    }

    /** A new composing string supersedes the pending temporary erase. */
    public synchronized void replaced(String text, Output output) {
        emptyDeletionPending = false;
        output.composing(text == null ? "" : text);
    }

    /** When no replacement follows, apply the erase after the short grace. */
    public synchronized boolean flush(Output output) {
        if (!emptyDeletionPending) return false;
        emptyDeletionPending = false;
        output.composing("");
        return true;
    }

    /** Commit may already replace the old composition; don't erase twice. */
    public synchronized void cancel() {
        emptyDeletionPending = false;
    }
    public synchronized boolean hasPending() {
        return emptyDeletionPending;
    }
}
