package com.alroot.wayland;

import android.app.Activity;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Map;

public final class TermuxSession {
    private static final String HOME = "/data/data/com.termux/files/home";
    private static final String PREFIX = "/data/data/com.termux/files/usr";
    private static final String SCRIPT = HOME + "/alroot_wayland/termux/alroot-wayland";

    private TermuxSession() {}

    public static boolean isCompatible(Activity activity) {
        try {
            PackageManager pm = activity.getPackageManager();
            int installedUid = pm.getApplicationInfo("com.termux", 0).uid;
            byte[] theirs = certificate(pm, "com.termux");
            byte[] ours = certificate(pm, activity.getPackageName());
            return installedUid == android.os.Process.myUid()
                    && theirs != null && ours != null
                    && MessageDigest.isEqual(theirs, ours);
        } catch (Exception e) {
            return false;
        }
    }


    public static String uidAndSigning(Activity activity) {
        try {
            PackageManager pm = activity.getPackageManager();
            ApplicationInfo termux = pm.getApplicationInfo("com.termux", 0);
            boolean uid = termux.uid == android.os.Process.myUid();
            byte[] theirs = certificate(pm, "com.termux");
            byte[] ours = certificate(pm, activity.getPackageName());
            boolean signing = theirs != null && ours != null && MessageDigest.isEqual(theirs, ours);
            return "helper UID " + android.os.Process.myUid() + " / Termux " + termux.uid
                    + "  UID " + (uid ? "MATCH" : "FAIL")
                    + "  signing " + (signing ? "MATCH" : "FAIL");
        } catch (Exception ex) {
            return "Termux UID/signature verification unavailable: " + ex.getClass().getSimpleName();
        }
    }

    private static byte[] certificate(PackageManager pm, String name) throws Exception {
        android.content.pm.PackageInfo pi;
        android.content.pm.Signature[] signatures;
        if (Build.VERSION.SDK_INT >= 28) {
            pi = pm.getPackageInfo(name, PackageManager.GET_SIGNING_CERTIFICATES);
            signatures = pi.signingInfo.hasMultipleSigners()
                    ? pi.signingInfo.getApkContentsSigners()
                    : pi.signingInfo.getSigningCertificateHistory();
        } else {
            pi = pm.getPackageInfo(name, PackageManager.GET_SIGNATURES);
            signatures = pi.signatures;
        }
        if (signatures == null || signatures.length == 0) return null;
        return MessageDigest.getInstance("SHA-256").digest(signatures[0].toByteArray());
    }

    public static String launch(String command, String profile) {
        if (!new File(SCRIPT).isFile()) return "Missing trusted CLI: " + SCRIPT;
        if (!("start".equals(command) || "stop".equals(command) || "status".equals(command)
                || "doctor".equals(command))) return "Rejected unknown action";
        try {
            java.util.List<String> argv = new java.util.ArrayList<>();
            argv.add(PREFIX + "/bin/bash");
            argv.add(SCRIPT);
            argv.add(command);
            if ("start".equals(command)) {
                if (!("xfce-labwc".equals(profile) || "xfce-shm-direct".equals(profile)))
                    return "Rejected profile";
                argv.add("--profile"); argv.add(profile);
            }
            ProcessBuilder process = new ProcessBuilder(argv);
            Map<String, String> env = process.environment();
            env.clear();
            env.put("HOME", HOME);
            env.put("PREFIX", PREFIX);
            env.put("PATH", PREFIX + "/bin:" + PREFIX + "/bin/applets");
            env.put("SHELL", PREFIX + "/bin/bash");
            env.put("TMPDIR", PREFIX + "/tmp");
            env.put("LANG", "ko_KR.UTF-8");
            env.put("TERM", "xterm-256color");
            File logfile = new File(HOME + "/.alroot-wayland/spawn.log");
            File parent = logfile.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("log dir");
            process.directory(new File(HOME));
            process.redirectErrorStream(true);
            process.redirectOutput(ProcessBuilder.Redirect.appendTo(logfile));
            process.start();
            Log.i("AlrootWayland", "trusted Termux action " + command + " spawned");
            return "Action " + command + " launched; see " + logfile;
        } catch (Exception ex) {
            Log.e("AlrootWayland", "Termux action failed", ex);
            return "Termux command failed: " + ex;
        }
    }
}
