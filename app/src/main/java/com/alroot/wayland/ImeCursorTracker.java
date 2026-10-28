package com.alroot.wayland;

/**
 * Android IME's synthetic editor position, independent of the remote X11
 * application. Termux-X11 needs this because Samsung/Gboard sometimes move
 * the fake selection past the one-character placeholder, then issue
 * deleteSurroundingText to remove that phantom context.
 *
 * Such a deletion must NOT reach X11. In P12 this request was treated as
 * a real Backspace and could trigger keyboard state resynchronization
 * and replay previously typed English after Hangul deletion.
 *
 * No real user text is stored by this class.
 */
public final class ImeCursorTracker {
    public static final class Report {
        public final int cursor;
        public final boolean reset;
        /** True only when the IME must be told that its selection changed. */
        public final boolean notify;
        Report(int cursor, boolean reset, boolean notify) {
            this.cursor = cursor;
            this.reset = reset;
            this.notify = notify;
        }
    }

    private int cursor = 1;
    private int requested = -1;
    private int batchDepth;
    private boolean reset;
    private int ignoredPhantomDeletes;
    // EditorInfo.initialSelStart/End are both 1. Reporting that SAME
    // selection on each rejected stale IME callback creates a feedback loop
    // in Samsung Keyboard: it retries the old English composition even
    // though the host never accepted that text.
    private int lastReportedCursor = 1;

    public synchronized void begin() {
        if (++batchDepth == 1) {
            requested = -1;
            reset = false;
        }
    }
    public synchronized boolean end() {
        if (batchDepth > 0) batchDepth--;
        if (batchDepth == 0) requested = -1;
        return batchDepth == 0;
    }
    public synchronized boolean inBatch() { return batchDepth > 0; }

    public synchronized void selected(int start, int end) {
        if (batchDepth > 0) {
            // The IME moved beyond our fake blank context. It sometimes
            // cleans up that phantom space with deleteSurroundingText.
            if (start == end && start > cursor) requested = start;
            return;
        }
        // Samsung IME can redundantly request the exact position it was
        // already given. Turning a no-op setSelection(1,1) into
        // updateSelection(-1,-1) -> (1,1) makes the keyboard repeatedly
        // rebuild its previous Latin context.
        if (start == end && start == cursor) return;
        // Outside a batch this is cursor/navigation control, not a real
        // X11 cursor move. Reset Samsung's editor-state anchor to 1.
        cursor = 1;
        requested = -1;
        reset = true;
    }
    public synchronized boolean phantomDelete(int before, int after) {
        if (before > 0 && requested > cursor) {
            requested = Math.max(cursor, requested - before);
            ignoredPhantomDeletes++;
            return true;
        }
        return false;
    }
    public synchronized void committed(int chars, int newPosition) {
        if (newPosition > 0) {
            cursor = Math.max(1, cursor + Math.max(0, chars) + newPosition - 1);
        } else {
            reset = true;
        }
    }
    public synchronized void deleted(int before) {
        cursor -= Math.max(0, before);
        if (cursor <= 1) reset = true;
    }
    public synchronized Report report() {
        boolean shouldReset = reset;
        if (shouldReset) cursor = 1;
        reset = false;
        boolean needsNotify = shouldReset || cursor != lastReportedCursor;
        if (needsNotify) lastReportedCursor = cursor;
        return new Report(cursor, shouldReset, needsNotify);
    }
    public synchronized int ignoredPhantomDeletes() {
        return ignoredPhantomDeletes;
    }
}
