package com.alroot.wayland;

import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Text-only clipboard bridge from the Android host to a separate rootful Xwayland. */
public final class ClipboardBridge {
    private static final long MAX_BYTES = 1024 * 1024;
    private static final File IPC =
            new File(HelperService.runtimeDir().getParentFile(), "ipc");
    private static final File TO_X11 = new File(IPC, "android-to-x11.txt");
    private static final File FROM_X11 = new File(IPC, "x11-to-android.txt");
    private static long lastModified = -1;
    private static long lastSize = -1;

    private ClipboardBridge() {}

    public static void androidToX11(String text) {
        if (text == null) return;
        byte[] utf8 = text.getBytes(StandardCharsets.UTF_8);
        if (utf8.length > MAX_BYTES) {
            Log.w("AlrootWayland", "clipboard text exceeds 1MiB bridge limit");
            return;
        }
        File temp = null;
        try {
            if (!IPC.isDirectory() && !IPC.mkdirs()) return;
            if (!IPC.setReadable(false, false) || !IPC.setWritable(false, false)
                    || !IPC.setExecutable(false, false)
                    || !IPC.setReadable(true, true) || !IPC.setWritable(true, true)
                    || !IPC.setExecutable(true, true)) return;
            temp = File.createTempFile("clip-", ".tmp", IPC);
            try (FileOutputStream output = new FileOutputStream(temp)) {
                output.write(utf8);
            }
            temp.setReadable(false, false);
            temp.setWritable(false, false);
            temp.setReadable(true, true);
            temp.setWritable(true, true);
            Files.move(temp.toPath(), TO_X11.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception ex) {
            Log.w("AlrootWayland", "Android -> X11 clipboard bridge failed", ex);
        } finally {
            if (temp != null) temp.delete();
        }
    }

    /** Null means no new clipboard data; empty string means a valid empty selection. */
    public static String updatedX11Text() {
        if (!FROM_X11.isFile()) return null;
        long length = FROM_X11.length();
        long modified = FROM_X11.lastModified();
        if (length > MAX_BYTES || (modified == lastModified && length == lastSize)) return null;
        try {
            byte[] bytes = Files.readAllBytes(FROM_X11.toPath());
            if (bytes.length > MAX_BYTES) return null;
            lastModified = modified;
            lastSize = length;
            return new String(bytes, StandardCharsets.UTF_8);
        } catch (Exception ex) {
            Log.w("AlrootWayland", "X11 -> Android clipboard bridge failed", ex);
            return null;
        }
    }
}
