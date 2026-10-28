import com.alroot.wayland.ImeCursorTracker;

public final class ImeCursorTrackerTest {
    static void check(boolean good, String reason) {
        if (!good) throw new AssertionError(reason);
    }
    public static void main(String[] args) {
        ImeCursorTracker tracker = new ImeCursorTracker();
        tracker.begin();
        tracker.committed(5, 1);
        check(tracker.report().cursor == 6, "English hello cursor");
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
        System.out.println("PASS: 13 fake-cursor and phantom deletion assertions");
    }
}
