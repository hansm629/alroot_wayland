package com.alroot.wayland;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * P19 diagnostic only: a short, rotating, text-free Android IME event trace.
 * Never persist user text, key values, Unicode code points or clipboard.
 * Used to compare the FIRST real Samsung editor sequence with the X11 daemon.
 *
 * All file I/O runs off the input/UI thread. The trace is private to the
 * Termux shared UID and bounded to 64KiB. It is not uploaded anywhere.
 */
final class ImeFlowTrace {
    private static final HandlerThread thread = new HandlerThread("AlrootIMETrace");
    private static final Handler io;
    private static final long MAX_BYTES = 65536L;
    private static final File target = new File(
            HelperService.runtimeDir().getParentFile(), "diagnostics/p19-ime-flow.log");
    private static long serial;
    // Owned by the handler thread. Keep one open FD, not a new file and
    // multiple chmod syscalls on every Samsung composing revision.
    private static FileOutputStream output;
    private static long writtenBytes;
    static {
        thread.start();
        io = new Handler(thread.getLooper());
    }

    private ImeFlowTrace() {}

    static String kind(CharSequence text) {
        if (text == null || text.length() == 0) return "EMPTY";
        boolean latin = false, korean = false, other = false;
        // Classification is bounded even if Samsung sends an oversized
        // composing buffer; always report full length but never its bytes.
        int end = Math.min(text.length(), 128);
        for (int i = 0; i < end; ) {
            int cp = Character.codePointAt(text, i);
            if (cp >= 0x20 && cp < 0x80) latin = true;
            else if ((cp >= 0x1100 && cp <= 0x11ff) ||
                    (cp >= 0x3130 && cp <= 0x318f) ||
                    (cp >= 0xac00 && cp <= 0xd7af)) korean = true;
            else other = true;
            i += Character.charCount(cp);
        }
        if (latin && korean) return "MIXED";
        if (korean) return "KOREAN";
        if (latin && !other) return "ASCII";
        return "OTHER";
    }

    static synchronized void record(String operation, CharSequence text, boolean delivered) {
        // operation is a hardcoded internal label, never user-supplied.
        final String type = kind(text);
        final int length = text == null ? 0 : text.length();
        final long number = ++serial;
        final long when = SystemClock.elapsedRealtime();
        final String line = number + " ms=" + when + " op=" + operation +
                " type=" + type + " chars=" + length +
                " forwarded=" + (delivered ? 1 : 0) + "\n";
        io.post(() -> {
            try {
                final byte[] bytes = line.getBytes(StandardCharsets.US_ASCII);
                if (output == null || writtenBytes + bytes.length > MAX_BYTES) {
                    if (output != null) {
                        output.close();
                        output = null;
                    }
                    File dir = target.getParentFile();
                    if (!dir.isDirectory() && !dir.mkdirs()) return;
                    // New diagnostic directory only. Do not change
                    // permissions of the user's pre-existing logs.
                    dir.setReadable(false, false);
                    dir.setWritable(false, false);
                    dir.setExecutable(false, false);
                    dir.setReadable(true, true);
                    dir.setWritable(true, true);
                    dir.setExecutable(true, true);
                    boolean append = target.isFile() &&
                            target.length() + bytes.length <= MAX_BYTES;
                    writtenBytes = append ? target.length() : 0;
                    output = new FileOutputStream(target, append);
                    target.setReadable(false, false);
                    target.setWritable(false, false);
                    target.setReadable(true, true);
                    target.setWritable(true, true);
                }
                output.write(bytes);
                writtenBytes += bytes.length;
            } catch (Exception ignored) {
                // The IME must always work even if the diagnostic cannot.
                if (output != null) {
                    try { output.close(); } catch (Exception alsoIgnored) {}
                }
                output = null;
            }
        });
    }
}
