package com.alroot.wayland;

public final class ImeConnectionHolderTest {
    private static void check(boolean value, String reason) {
        if (!value) throw new AssertionError(reason);
    }
    public static void main(String[] args) {
        final class FakeInputConnection {
            final ImeCursorTracker fakeCursor = new ImeCursorTracker();
        }
        ImeConnectionHolder<FakeInputConnection> firstView = new ImeConnectionHolder<>();
        FakeInputConnection firstConnection = firstView.getOrCreate(FakeInputConnection::new);
        check(firstView.hasConnection(), "First keyboard open creates a connection");
        firstConnection.fakeCursor.committed(5, 1);
        check(firstConnection.fakeCursor.report().cursor == 6,
                "English state is shared by the first connection");
        for (int n = 0; n < 100; ++n) {
            FakeInputConnection reused = firstView.getOrCreate(() -> {
                throw new AssertionError("Second onCreateInputConnection created a new editor");
            });
            check(firstConnection == reused, "Same view must retain the same connection");
            check(reused.fakeCursor.report().cursor == 6,
                    "Repeated IME connection request must keep initial English cursor");
        }
        ImeConnectionHolder<FakeInputConnection> nextView = new ImeConnectionHolder<>();
        check(nextView.getOrCreate(FakeInputConnection::new) != firstConnection,
                "A genuinely new View must not reuse the old editor");
        System.out.println("PASS: initial/repeated Android IME connection retains identity");
    }
}
