package com.alroot.wayland;

import android.os.SystemClock;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

/** Android IME text commitments into the isolated rootful Xwayland XTEST worker. */
public final class ImeBridge {
    private static final File STATE = HelperService.runtimeDir().getParentFile();
    private static final File QUEUE = new File(STATE, "ipc/ime-requests");
    private static final File READY = new File(STATE, "ime-active");
    private static final AtomicInteger NEXT = new AtomicInteger();
    private static String pendingComposition = "";
    private ImeBridge() {}

    public static boolean rootfulX11Active() {
        File display = new File(STATE, "display");
        File identity = new File(STATE, "xwayland.pid");
        if (!display.isFile() || !identity.isFile() || !READY.isFile()) return false;
        try {
            String name = new String(Files.readAllBytes(display.toPath()),
                    StandardCharsets.US_ASCII).trim();
            String marker = new String(Files.readAllBytes(READY.toPath()),
                    StandardCharsets.US_ASCII).trim();
            String pidRecord = new String(Files.readAllBytes(identity.toPath()),
                    StandardCharsets.US_ASCII).trim();
            // The marker is removed on explicit stop and before every new
            // session. This path uses no cross-process /proc reads.
            return name.matches(":[0-9]{1,3}") && !":0".equals(name)
                    && name.equals(marker) && pidRecord.endsWith(" alroot-wayland");
        } catch (Exception e) {
            Log.w("AlrootWayland", "IME X11 readiness marker unavailable", e);
            return false;
        }
    }

    public static void commit(String value) {
        if (value == null || value.isEmpty()) return;
        byte[] content = value.getBytes(StandardCharsets.UTF_8);
        if (content.length > 16384) {
            Log.w("AlrootWayland", "IME commit above bounded 16KiB limit");
            return;
        }
        offer("commit", content);
        Log.i("AlrootWayland", "IME X11 committed chars=" + value.length()
                + " UTF8bytes=" + content.length);
    }

    /**
     * Samsung/Gboard commonly send setComposingText repeatedly, then finish
     * composing (or commitText with an empty string). Since rootful Xwayland
     * doesn't understand Android text-input preedit, maintain exactly one
     * replaceable X11 composing span. Never wait for a nonempty commitText.
     */
    public static synchronized void composing(String text) {
        if (text == null) text = "";
        if (text.equals(pendingComposition)) return;
        if (!pendingComposition.isEmpty()) {
            delete(pendingComposition.codePointCount(0, pendingComposition.length()), 0);
        }
        pendingComposition = text;
        if (!text.isEmpty()) commit(text);
        Log.i("AlrootWayland", "IME X11 composing length=" + text.length());
    }

    public static synchronized void commitComposed(String text) {
        if (text == null) text = "";
        if (text.isEmpty()) {
            // Samsung IME can terminate a composition with commitText("").
            // The preedit was already drawn in the X11 app; do not erase it.
            boolean hadComposition = !pendingComposition.isEmpty();
            pendingComposition = "";
            Log.i("AlrootWayland", "IME X11 empty-commit finalized preedit="
                    + hadComposition);
            return;
        }
        if (!pendingComposition.isEmpty() && !text.equals(pendingComposition)) {
            delete(pendingComposition.codePointCount(0, pendingComposition.length()), 0);
        }
        boolean same = !pendingComposition.isEmpty() && text.equals(pendingComposition);
        pendingComposition = "";
        if (!same && !text.isEmpty()) commit(text);
        Log.i("AlrootWayland", "IME X11 finalized length=" + text.length()
                + " composition-reused=" + same);
    }

    public static synchronized void finishComposing() {
        // The latest preedit has already been sent into the X11 text field.
        pendingComposition = "";
    }

    public static synchronized void resetComposing() {
        pendingComposition = "";
    }

    public static void delete(int before, int after) {
        int b = Math.max(0, Math.min(128, before));
        int a = Math.max(0, Math.min(128, after));
        if (b > 0 || a > 0)
            offer("delete", (b + " " + a).getBytes(StandardCharsets.US_ASCII));
    }

    private static void offer(String kind, byte[] data) {
        File temp = null;
        try {
            if (!QUEUE.isDirectory() && !QUEUE.mkdirs()) return;
            if (!QUEUE.setReadable(false, false) || !QUEUE.setWritable(false, false)
                    || !QUEUE.setExecutable(false, false)
                    || !QUEUE.setReadable(true, true) || !QUEUE.setWritable(true, true)
                    || !QUEUE.setExecutable(true, true)) return;
            temp = File.createTempFile("ime-", ".tmp", QUEUE);
            try (FileOutputStream stream = new FileOutputStream(temp)) {
                stream.write(data);
            }
            String filename = String.format(Locale.ROOT, "%013d-%010d.%s",
                    SystemClock.elapsedRealtime(), NEXT.incrementAndGet(), kind);
            Files.move(temp.toPath(), new File(QUEUE, filename).toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception e) {
            Log.w("AlrootWayland", "IME queue unavailable", e);
        } finally {
            if (temp != null) temp.delete();
        }
    }
}
