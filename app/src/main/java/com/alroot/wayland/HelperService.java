package com.alroot.wayland;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import com.droiddeck.launcher.wayland.WaylandCompositor;
import java.io.File;

public final class HelperService extends Service {
    public static final String ACTION_STOP = "com.alroot.wayland.STOP";
    private static final String CHANNEL = "alroot_wayland_session";
    private static final int NOTIFICATION = 30721;
    public static volatile boolean running;
    public static volatile String error;

    public static File runtimeDir() {
        return new File("/data/data/com.termux/files/home/.alroot-wayland/runtime");
    }

    @Override public void onCreate() {
        super.onCreate();
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(
                CHANNEL, "Alroot Wayland session", NotificationManager.IMPORTANCE_LOW));
        Intent launch = new Intent(this, MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent open = PendingIntent.getActivity(this, 0, launch,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification notice = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.ic_menu_view)
                .setContentTitle("Alroot Wayland")
                .setContentText("Local display session — tap to reopen")
                .setOngoing(true).setContentIntent(open).build();
        if (Build.VERSION.SDK_INT >= 34)
            startForeground(NOTIFICATION, notice, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        else startForeground(NOTIFICATION, notice);
    }

    @Override public int onStartCommand(Intent intent, int flags, int id) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            running = false;
            if (error == null) {
                try { WaylandCompositor.nativeStop(); } catch (Throwable e) {
                    Log.e("AlrootWayland", "native stop failed", e);
                }
            }
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return START_NOT_STICKY;
        }
        if (!running) {
            try {
                File dir = runtimeDir();
                if (!dir.isDirectory() && !dir.mkdirs())
                    throw new IllegalStateException("Cannot create " + dir);
                if (!dir.setReadable(false, false) || !dir.setWritable(false, false)
                        || !dir.setExecutable(false, false)
                        || !dir.setReadable(true, true) || !dir.setWritable(true, true)
                        || !dir.setExecutable(true, true))
                    throw new IllegalStateException("Cannot secure Wayland runtime directory");

                int w = getSharedPreferences("display", MODE_PRIVATE).getInt("width", 1280);
                int h = getSharedPreferences("display", MODE_PRIVATE).getInt("height", 720);
                WaylandCompositor.nativeSetOutputSize(w, h);
                WaylandCompositor.nativeSetZeroCopy(false);
                WaylandCompositor.nativeStartWithSurface(null, dir.getAbsolutePath(),
                        null, null, getApplicationInfo().nativeLibraryDir);
                running = true;
                error = null;
                Log.i("AlrootWayland", "server launched; awaiting actual wayland-0 socket");
            } catch (Throwable e) {
                error = e.toString();
                Log.e("AlrootWayland", "server start failed", e);
            }
        }
        return START_STICKY;
    }

    @Override public void onDestroy() {
        if (running) {
            try { WaylandCompositor.nativeStop(); } catch (Throwable ignored) {}
        }
        running = false;
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
