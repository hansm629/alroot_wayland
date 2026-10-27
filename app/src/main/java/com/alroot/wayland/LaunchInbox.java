package com.alroot.wayland;

import android.app.Activity;
import android.os.Process;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/**
 * The matching-UID Termux launcher leaves a fixed-profile request in a
 * private directory. The foreground Activity validates and consumes it.
 * Android Intents do not carry commands, arguments or an exported shell.
 */
public final class LaunchInbox {
    private static final File CONTROL = new File(
            "/data/data/com.termux/files/home/.alroot-wayland/control");
    private static final File REQUEST = new File(CONTROL, "start-request");
    private static final File ACK = new File(CONTROL, "start-ack");
    private static final String TAG = "AlrootWayland";

    private LaunchInbox() {}

    private static boolean trusted(File file, boolean directory) {
        try {
            StructStat stat = Os.lstat(file.getAbsolutePath());
            boolean type = directory ? OsConstants.S_ISDIR(stat.st_mode)
                    : OsConstants.S_ISREG(stat.st_mode);
            return type && stat.st_uid == Process.myUid() && (stat.st_mode & 0077) == 0;
        } catch (Exception ignored) {
            return false;
        }
    }

    private static void acknowledge(String id, String status) {
        File temp = null;
        try {
            temp = File.createTempFile(".ack.", ".tmp", CONTROL);
            Os.chmod(temp.getAbsolutePath(), 0600);
            try (FileOutputStream out = new FileOutputStream(temp)) {
                out.write((id + " " + status + "\n")
                        .getBytes(StandardCharsets.US_ASCII));
            }
            Files.move(temp.toPath(), ACK.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception e) {
            Log.e(TAG, "Cannot acknowledge private launch request", e);
        } finally {
            if (temp != null) temp.delete();
        }
    }

    public static void consume(Activity activity) {
        if (!activity.hasWindowFocus() || !HelperService.running) return;
        if (!new File(HelperService.runtimeDir(), "wayland-0").exists()) return;
        if (!trusted(CONTROL, true) || !trusted(REQUEST, false)) return;

        String request;
        try {
            if (REQUEST.length() < 20 || REQUEST.length() > 160) {
                Log.w(TAG, "Malformed launch request size");
                REQUEST.delete();
                return;
            }
            request = new String(Files.readAllBytes(REQUEST.toPath()),
                    StandardCharsets.US_ASCII).trim();
        } catch (Exception ex) {
            Log.e(TAG, "Cannot read private launch request", ex);
            return;
        }
        String[] parts = request.split(" ");
        if (parts.length != 4 || !"start".equals(parts[0])
                || !("xfce-labwc".equals(parts[1]) || "xfce-shm-direct".equals(parts[1]))
                || !parts[2].matches("[0-9]{10,12}")
                || !parts[3].matches("[0-9]{10,12}-[0-9]{1,10}")) {
            REQUEST.delete();
            Log.w(TAG, "Rejected malformed/non-allowlisted launch request");
            return;
        }
        String id = parts[3];
        long age;
        try {
            age = Math.abs(System.currentTimeMillis() / 1000L - Long.parseLong(parts[2]));
        } catch (NumberFormatException ex) {
            age = Long.MAX_VALUE;
        }
        if (age > 90 || !TermuxSession.isCompatible(activity)) {
            REQUEST.delete();
            acknowledge(id, age > 90 ? "EXPIRED" : "UID_SIGNING_MISMATCH");
            return;
        }
        // Remove the request before ProcessBuilder so Activity recreation cannot
        // replay and create duplicate Xwayland/XFCE children.
        if (!REQUEST.delete()) {
            acknowledge(id, "DELETE_FAILED");
            return;
        }
        String result = TermuxSession.launch("start", parts[1]);
        if (result.startsWith("Action start launched")) {
            acknowledge(id, "REQUESTED");
            Log.i(TAG, "Foreground Activity requested " + parts[1]);
        } else {
            acknowledge(id, "SPAWN_FAILED");
            Log.e(TAG, "Session spawn failed: " + result);
        }
    }
}
