package com.alroot.wayland;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.system.Os;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import org.json.JSONObject;

/**
 * Bounded state files shared with the same-UID native Termux session.
 * No Android components are exported and no shell input is accepted.
 */
public final class SessionFiles {
    private static final File ROOT = HelperService.runtimeDir().getParentFile();
    private SessionFiles() {}

    private static void put(String filename, String value) {
        File temp = null;
        try {
            if (!ROOT.isDirectory() && !ROOT.mkdirs()) throw new Exception("create state directory");
            Os.chmod(ROOT.getAbsolutePath(), 0700);
            temp = File.createTempFile(".alroot-", ".tmp", ROOT);
            Os.chmod(temp.getAbsolutePath(), 0600);
            try (FileOutputStream out = new FileOutputStream(temp)) {
                out.write(value.getBytes(StandardCharsets.UTF_8));
            }
            Files.move(temp.toPath(), new File(ROOT, filename).toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception e) {
            Log.e("AlrootWayland", "State write " + filename + " failed", e);
        } finally {
            if (temp != null) temp.delete();
        }
    }

    public static void setResolution(int w, int h) {
        if (w < 320 || w > 3840 || h < 240 || h > 2160) return;
        put("output-resolution", w + " " + h + "\n");
        Log.i("AlrootWayland", "Guest resolution configured " + w + "x" + h + " @96 DPI");
    }

    public static void snapshotBattery(Context context) {
        try {
            Intent battery = context.registerReceiver(null,
                    new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (battery == null) return;
            int level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            if (level < 0 || scale <= 0) return;
            int percentage = Math.max(0, Math.min(100, Math.round(level * 100f / scale)));
            int code = battery.getIntExtra(BatteryManager.EXTRA_STATUS,
                    BatteryManager.BATTERY_STATUS_UNKNOWN);
            String status = code == BatteryManager.BATTERY_STATUS_CHARGING ? "CHARGING"
                    : code == BatteryManager.BATTERY_STATUS_FULL ? "FULL"
                    : code == BatteryManager.BATTERY_STATUS_DISCHARGING ? "DISCHARGING"
                    : code == BatteryManager.BATTERY_STATUS_NOT_CHARGING ? "NOT_CHARGING"
                    : "UNKNOWN";
            JSONObject payload = new JSONObject();
            payload.put("percentage", percentage);
            payload.put("status", status);
            payload.put("time_ms", System.currentTimeMillis());
            put("android-battery.json", payload.toString() + "\n");
        } catch (Exception e) {
            Log.w("AlrootWayland", "Android battery snapshot unavailable", e);
        }
    }
}
