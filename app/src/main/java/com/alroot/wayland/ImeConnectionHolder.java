package com.alroot.wayland;

/**
 * Keep one Android InputConnection per SurfaceView, as Termux-X11 does.
 *
 * Samsung may call onCreateInputConnection more than once on the first
 * keyboard open (observed twice in one millisecond on S26U). Returning a
 * fresh connection each time loses the fake-editor selection/composition
 * state while the keyboard still has callbacks queued for the old editor.
 * A new SurfaceView gets a new holder; an Activity recreation is distinct.
 */
final class ImeConnectionHolder<T> {
    interface Factory<T> { T create(); }
    private T connection;

    synchronized boolean hasConnection() { return connection != null; }

    synchronized T getOrCreate(Factory<T> factory) {
        if (connection == null) {
            connection = factory.create();
            if (connection == null) throw new IllegalStateException("null IME connection");
        }
        return connection;
    }
}
