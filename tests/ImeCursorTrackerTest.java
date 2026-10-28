import com.alroot.wayland.ImeCursorTracker;

public final class ImeCursorTrackerTest {
    static void check(boolean good, String reason) {
        if (!good) throw new AssertionError(reason);
    }
    public static void main(String[] args) {
        ImeCursorTracker tracker = new ImeCursorTracker();
        tracker.selected(1, 1);
        check(!tracker.report().notify,
                "Same-position Samsung setSelection must not restart editor state");
        tracker.begin();
        tracker.committed(5, 1);
        check(tracker.report().cursor == 6, "English hello cursor");
        check(!tracker.report().notify,
                "Identical synthetic selection must not be published again");
        tracker.selected(6, 6);
        check(!tracker.report().notify,
                "No-op setSelection(6,6) must not reset to fake anchor");
        tracker.selected(9, 9);
        check(tracker.phantomDelete(2, 0), "Ignore fake IME whitespace deletion");
        check(tracker.phantomDelete(1, 0), "Ignore trailing fake whitespace");
        check(!tracker.phantomDelete(1, 0), "Next real deletion must be forwarded");
        tracker.deleted(1);
        check(tracker.end(), "Batch is closed");
        check(tracker.report().cursor == 5, "One actual Backspace");
        check(tracker.ignoredPhantomDeletes() == 2, "Two phantom deletes counted");

        tracker.begin();
        tracker.selected(10, 10);
        tracker.selected(5, 5);
        check(tracker.phantomDelete(1, 0), "Cursor cleanup while in batch");
        tracker.end();
        tracker.selected(0, 0);
        ImeCursorTracker.Report report = tracker.report();
        check(report.reset && report.cursor == 1, "Outside-batch selection resets anchor");

        tracker.begin();
        tracker.committed(1, 1);
        tracker.end();
        check(tracker.report().cursor == 2, "New English input still permitted");
        tracker.deleted(1);
        check(tracker.report().cursor == 1, "Committed delete returns to anchor");
        check(!tracker.phantomDelete(1, 0),
                "Phantom marker must not leak after batch ends");
        check(!tracker.report().notify,
                "Duplicate no-op batch/endBatchEdit must not notify Samsung");
        tracker.selected(0, 0);
        ImeCursorTracker.Report forced = tracker.report();
        check(forced.reset && forced.notify && forced.cursor == 1,
                "Explicit editor selection reset always notified once");
        check(!tracker.report().notify,
                "Selection reset acknowledged, not repeated forever");
        System.out.println("PASS: synthetic cursor, phantom deletion and no-feedback selection");
    }
}
