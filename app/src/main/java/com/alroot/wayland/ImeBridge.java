package com.alroot.wayland;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Locale;

/**
 * Android IME -> dedicated persistent rootful-Xwayland input daemon.
 *
 * Compared with the old legacy per-keystroke xclip/xdotool bridge this avoids
 * creating temp files, forking processes and sleeping 350-500ms on every
 * Hangul composition change. All socket writes happen off Android's UI thread.
 */
public final class ImeBridge {
    private static final String TAG = "AlrootWayland";
    private static final File STATE = HelperService.runtimeDir().getParentFile();
    private static final File READY = new File(STATE, "ime-active");
    private static final File SOCKET = new File(STATE, "ipc/ime.sock");
    private static final File QUEUE = new File(STATE, "ipc/ime-requests");
    private static final HandlerThread ioThread = new HandlerThread("AlrootX11IME");
    private static final Handler io;
    private static final ImeCommitState composition = new ImeCommitState();
    private static boolean asciiComposing;
    private static String asciiDraft = "";
    private static final ImeCommitState.Sink compositionSink =
            new ImeCommitState.Sink() {
                @Override public void committed(String text) {
                    io.post(() -> send('T', text));
                }
                @Override public void delete(int before, int after) {
                    io.post(() -> send('D', before + " " + after));
                }
            };
    private static LocalSocket socket;
    private static OutputStream stream;
    private static String fallbackComposition = "";
    private static long lastProbeMs;
    private static boolean lastActive;
    private static int delivered, fallbacks;
    private static long totalTransportNs;
    private static int fileSerial;

    static {
        ioThread.start();
        io = new Handler(ioThread.getLooper());
    }

    private ImeBridge() {}

    public static boolean rootfulX11Active() {
        final long now = SystemClock.elapsedRealtime();
        synchronized (ImeBridge.class) {
            if (now - lastProbeMs < 250) return lastActive;
            lastProbeMs = now;
            boolean okay = false;
            try {
                File display = new File(STATE, "display");
                File identity = new File(STATE, "xwayland.pid");
                if (display.isFile() && identity.isFile() && READY.isFile()) {
                    String name = new String(Files.readAllBytes(display.toPath()),
                            StandardCharsets.US_ASCII).trim();
                    String marker = new String(Files.readAllBytes(READY.toPath()),
                            StandardCharsets.US_ASCII).trim();
                    String pidRecord = new String(Files.readAllBytes(identity.toPath()),
                            StandardCharsets.US_ASCII).trim();
                    okay = name.matches(":[0-9]{1,3}") && !":0".equals(name)
                            && name.equals(marker) && pidRecord.endsWith(" alroot-wayland");
                }
            } catch (Exception e) {
                Log.w(TAG, "IME X11 readiness state unavailable", e);
            }
            lastActive = okay;
            return okay;
        }
    }

    private static boolean asciiOnly(String text) {
        if (text == null || text.isEmpty()) return false;
        for (int i = 0; i < text.length(); i++) {
            if (text.charAt(i) > 0x7f) return false;
        }
        return true;
    }

    private static void finalizeNativeAscii(long ms) {
        if (!asciiComposing) return;
        composition.alreadyInsertedAscii(asciiDraft, ms);
        asciiComposing = false;
        asciiDraft = "";
        io.post(() -> send('F', ""));
    }

    public static synchronized void composing(String text) {
        long ms = SystemClock.elapsedRealtime();
        if (asciiOnly(text)) {
            // The original English path is already smooth: keep incremental
            // ASCII composing in XTEST, rather than delaying it until space.
            if (composition.hasDraft())
                composition.finished(ms, compositionSink);
            asciiComposing = true;
            asciiDraft = text;
            final String value = text;
            io.post(() -> send('C', value));
            return;
        }
        if (asciiComposing) {
            // The visible English preedit is already present in X11.
            // Finalize the native composing range without retransmitting it.
            finalizeNativeAscii(ms);
        }
        // Mutable Korean preedit stays in the Android IME. Only syllables
        // proven stable by a later event are transmitted to Xwayland.
        // This removes every speculative Backspace/insert repaint.
        composition.composing(text, ms, compositionSink);
    }

    public static synchronized void commitComposed(String text) {
        long ms = SystemClock.elapsedRealtime();
        if (asciiComposing && asciiOnly(text)) {
            asciiComposing = false;
            asciiDraft = "";
            composition.alreadyInsertedAscii(text, ms);
            final String value = text;
            // Native 'T' finalizes the existing P06 ASCII preedit without
            // reinserting the word, and leaves it visible at the X11 caret.
            io.post(() -> send('T', value));
            return;
        }
        boolean directKoreanSwitch = asciiComposing && text != null &&
                !text.isEmpty() && !asciiOnly(text);
        finalizeNativeAscii(ms);
        if (directKoreanSwitch) composition.keyboardSwitchedToKorean(ms);
        composition.committed(text, ms, compositionSink);
    }

    public static void commit(String text) { commitComposed(text); }

    public static synchronized void finishComposing() {
        if (asciiComposing) {
            finalizeNativeAscii(SystemClock.elapsedRealtime());
            return;
        }
        composition.finished(SystemClock.elapsedRealtime(), compositionSink);
        io.post(() -> send('F', ""));
    }

    public static synchronized void resetComposing() {
        finalizeNativeAscii(SystemClock.elapsedRealtime());
        composition.reset();
        io.post(() -> send('R', ""));
    }

    public static synchronized void delete(int before, int after) {
        if (asciiComposing) {
            // Preserve the English composing range that is already visible
            // inside X11. Deleting one composing character is a native C
            // revision, not a blind D+reset followed by duplicate retyping.
            int cps = asciiDraft.codePointCount(0, asciiDraft.length());
            int remain = Math.max(0, cps - Math.min(before, cps));
            asciiDraft = asciiDraft.substring(0,
                    asciiDraft.offsetByCodePoints(0, remain));
            final String left = asciiDraft;
            io.post(() -> send('C', left));
            int remainingBefore = Math.max(0, before - cps);
            if (remainingBefore + after > 0)
                io.post(() -> send('D', remainingBefore + " " + after));
            return;
        }
        composition.delete(before, after, compositionSink);
    }

    public static boolean hasPendingPreedit() {
        return composition.hasDraft();
    }

    /** A software-keyboard DEL of uncommitted preedit must not erase X11. */
    public static boolean deletePendingPreedit() {
        if (!composition.hasDraft()) return false;
        composition.delete(1, 0, compositionSink);
        return true;
    }

    private static boolean sendSocket(char operation, byte[] bytes) {
        final long beginNs = SystemClock.elapsedRealtimeNanos();
        try {
            if (stream == null) {
                socket = new LocalSocket();
                socket.connect(new LocalSocketAddress(
                        SOCKET.getAbsolutePath(), LocalSocketAddress.Namespace.FILESYSTEM));
                stream = socket.getOutputStream();
                Log.i(TAG, "IME persistent Unix socket connected");
            }
            byte[] packet = new byte[bytes.length + 5];
            packet[0] = (byte) operation;
            int n = bytes.length;
            packet[1] = (byte) (n >>> 24);
            packet[2] = (byte) (n >>> 16);
            packet[3] = (byte) (n >>> 8);
            packet[4] = (byte) n;
            System.arraycopy(bytes, 0, packet, 5, n);
            stream.write(packet);
            delivered++;
            totalTransportNs += SystemClock.elapsedRealtimeNanos() - beginNs;
            if (delivered % 64 == 0) {
                Log.i(TAG, "IME socket sent=" + delivered +
                        " average-transport-us=" + (totalTransportNs / delivered / 1000) +
                        " fallback=" + fallbacks);
            }
            return true;
        } catch (Exception ex) {
            closeConnection();
            if (fallbacks++ == 0 || fallbacks % 50 == 0)
                Log.w(TAG, "IME daemon unavailable; using legacy X11 input fallback");
            return false;
        }
    }

    private static void closeConnection() {
        try { if (stream != null) stream.close(); } catch (Exception ignored) {}
        try { if (socket != null) socket.close(); } catch (Exception ignored) {}
        stream = null;
        socket = null;
        fallbackComposition = "";
    }

    private static void send(char operation, String payload) {
        if (payload == null) payload = "";
        byte[] data = payload.getBytes(StandardCharsets.UTF_8);
        if (data.length > 16384) {
            Log.w(TAG, "IME payload exceeds bounded 16KiB");
            return;
        }
        if (!sendSocket(operation, data)) {
            // Never revive the old Unicode->X11-CLIPBOARD fallback. It
            // generated the Android "copied to connected device" toast and
            // could paste an unrelated clipboard selection into the editor.
            // Preserve plain-ASCII input via legacy XTEST, but report a
            // bounded failure for Unicode until the native daemon recovers.
            boolean unicode = false;
            for (byte b : data) if ((b & 0x80) != 0) { unicode = true; break; }
            if (unicode && (operation == 'C' || operation == 'T')) {
                Log.e(TAG, "IME Unicode temporarily unavailable: XTEST daemon is down; "
                        + "clipboard fallback is prohibited");
                return;
            }
            legacy(operation, payload);
        }
    }

    private static void legacy(char operation, String value) {
        // Strict compatibility fallback only. A healthy P04 daemon never
        // performs these old temp-file/xdotool/xclip operations.
        if (operation == 'C') {
            if (value.equals(fallbackComposition)) return;
            if (!fallbackComposition.isEmpty())
                enqueue("delete", fallbackComposition.codePointCount(
                        0, fallbackComposition.length()) + " 0");
            fallbackComposition = value;
            if (!value.isEmpty()) enqueue("commit", value);
        } else if (operation == 'T') {
            if (!value.isEmpty()) {
                if (!fallbackComposition.isEmpty() && !value.equals(fallbackComposition)) {
                    enqueue("delete", fallbackComposition.codePointCount(
                            0, fallbackComposition.length()) + " 0");
                }
                if (!value.equals(fallbackComposition))
                    enqueue("commit", value);
            }
            fallbackComposition = "";
        } else if (operation == 'D') {
            fallbackComposition = "";
            enqueue("delete", value);
        } else if (operation == 'F' || operation == 'R') {
            fallbackComposition = "";
        }
    }

    private static void enqueue(String kind, String value) {
        if (value.isEmpty()) return;
        File tmp = null;
        try {
            if (!QUEUE.isDirectory() && !QUEUE.mkdirs()) return;
            QUEUE.setReadable(false, false);
            QUEUE.setWritable(false, false);
            QUEUE.setExecutable(false, false);
            QUEUE.setReadable(true, true);
            QUEUE.setWritable(true, true);
            QUEUE.setExecutable(true, true);
            tmp = File.createTempFile(".legacy-ime-", ".tmp", QUEUE);
            try (FileOutputStream f = new FileOutputStream(tmp)) {
                f.write(value.getBytes(StandardCharsets.UTF_8));
            }
            String filename = String.format(Locale.ROOT, "%013d-%010d.%s",
                    SystemClock.elapsedRealtime(), fileSerial++, kind);
            Files.move(tmp.toPath(), new File(QUEUE, filename).toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception e) {
            Log.e(TAG, "IME legacy fallback enqueue failed", e);
        } finally {
            if (tmp != null) tmp.delete();
        }
    }
}
