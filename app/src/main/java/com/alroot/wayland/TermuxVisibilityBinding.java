package com.alroot.wayland;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.KeyguardManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.SystemClock;
import android.system.Os;
import android.util.AtomicFile;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import org.json.JSONArray;
import org.json.JSONObject;

/** Framework importance dependency for the existing, same-UID Termux service. */
final class TermuxVisibilityBinding {
    private static final String TAG = "AlrootWayland";
    private static final ComponentName SERVICE =
            new ComponentName("com.termux", "com.termux.app.TermuxService");
    private static final int FLAGS = Context.BIND_IMPORTANT | Context.BIND_ADJUST_WITH_ACTIVITY;
    private final Activity activity;
    private final EvidenceWriter evidence;
    private Connection connection;
    private boolean started, resumed, focused, topResumed, topResumedKnown;
    private boolean surfaceAttached, hostRunning, eligibleEpisode, closed;
    private Boolean compatible;
    private String state = "idle";
    private String lastError = "";

    TermuxVisibilityBinding(Activity activity) {
        this.activity = activity;
        evidence = EvidenceWriter.get(activity.getApplicationContext());
        record("created");
    }

    void started() { started = true; reconcile(); record("started"); }
    void stopped() { started = false; reconcile(); record("stopped"); }
    void resumed(boolean value) { resumed = value; record(value ? "resumed" : "paused"); }
    void focus(boolean value) { focused = value; record(value ? "focus_gained" : "focus_lost"); }
    void topResumed(boolean value) {
        topResumedKnown = true;
        topResumed = value;
        record(value ? "top_resumed" : "top_paused");
    }
    void note(String event) { record(event); }

    void display(boolean attached, boolean running) {
        if (closed || (surfaceAttached == attached && hostRunning == running)) return;
        surfaceAttached = attached;
        hostRunning = running;
        reconcile();
        record("display_dependency_changed");
    }

    private void reconcile() {
        boolean eligible = !closed && started && surfaceAttached && hostRunning;
        if (!eligible) {
            eligibleEpisode = false;
            release("idle");
            return;
        }
        // One attempt per visible-display episode. Existing status polling can
        // notice host readiness, but neither it nor a failed callback retries.
        if (eligibleEpisode) return;
        eligibleEpisode = true;
        compatible = TermuxSession.isCompatible(activity);
        if (!compatible) {
            state = "incompatible_uid_or_signature";
            record("bind_rejected");
            return;
        }
        Connection attempt = new Connection();
        connection = attempt;
        state = "binding";
        lastError = "";
        try {
            // Keep the exact Intent filter identity used by TermuxActivity.
            // Do not add an action/package/data/category or AUTO_CREATE: this
            // dependency must neither create nor stop the terminal service.
            boolean accepted = activity.bindService(new Intent().setComponent(SERVICE),
                    attempt, FLAGS);
            if (!accepted) release("bind_not_accepted");
            record(accepted ? "bind_requested" : "bind_not_accepted");
        } catch (RuntimeException ex) {
            lastError = ex.getClass().getSimpleName();
            release("bind_exception");
            record("bind_exception");
        }
    }

    private void release(String nextState) {
        Connection old = connection;
        connection = null;
        state = nextState;
        if (old == null) return;
        // Also required after a false return/exception: the framework may have
        // registered a dispatcher. No service lifecycle command is issued.
        try { activity.unbindService(old); }
        catch (IllegalArgumentException ignored) { /* No remaining registration. */ }
        catch (RuntimeException ex) { lastError = ex.getClass().getSimpleName(); }
    }

    void close() {
        if (closed) return;
        closed = true;
        started = false;
        eligibleEpisode = false;
        release("closed");
        record("destroyed");
    }

    private final class Connection implements ServiceConnection {
        private boolean current() { return !closed && connection == this; }
        @Override public void onServiceConnected(ComponentName name, IBinder binder) {
            if (!current()) return;
            if (!SERVICE.equals(name) || binder == null) {
                release("unexpected_binding");
                record("unexpected_binding");
                return;
            }
            // Termux exposes a local Binder, not a cross-process RPC contract.
            // Importance propagation needs only the framework connection.
            state = "connected";
            record("connected");
        }
        @Override public void onServiceDisconnected(ComponentName name) {
            if (!current()) return;
            // This registration remains valid; Android may reconnect it.
            state = "disconnected_registered";
            record("disconnected");
        }
        @Override public void onBindingDied(ComponentName name) {
            if (!current()) return;
            release("binding_died");
            record("binding_died");
        }
        @Override public void onNullBinding(ComponentName name) {
            if (!current()) return;
            release("null_binding");
            record("null_binding");
        }
    }

    private void record(String event) {
        try {
            JSONObject doc = new JSONObject();
            doc.put("schema", "alroot-activity-scheduler-v1");
            doc.put("event", event);
            doc.put("event_wall_ms", System.currentTimeMillis());
            doc.put("event_elapsed_ns", SystemClock.elapsedRealtimeNanos());
            doc.put("activity_instance", Integer.toHexString(System.identityHashCode(activity)));
            doc.put("pid", android.os.Process.myPid());
            doc.put("uid", android.os.Process.myUid());
            doc.put("started", started);
            doc.put("resumed", resumed);
            doc.put("focused", focused);
            doc.put("top_resumed_known", topResumedKnown);
            doc.put("top_resumed", topResumed);
            doc.put("pip", activity.isInPictureInPictureMode());
            doc.put("finishing", activity.isFinishing());
            doc.put("destroyed", activity.isDestroyed());
            doc.put("surface_attached_valid", surfaceAttached);
            doc.put("host_running", hostRunning);
            doc.put("binding_state", state);
            doc.put("binding_registered", connection != null);
            doc.put("binding_flags", FLAGS);
            doc.put("service_component", SERVICE.flattenToString());
            doc.put("compatible_uid_and_signature", compatible == null ? JSONObject.NULL : compatible);
            doc.put("last_error", lastError);
            doc.put("display_state", activity.getWindowManager().getDefaultDisplay().getState());
            doc.put("keep_screen_on", (activity.getWindow().getAttributes().flags
                    & android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0);
            int tid = hostRunning ? WaylandCompositor.nativeCompositorTid() : 0;
            doc.put("compositor_tid", tid);
            evidence.post(doc);
            Log.i(TAG, "ActivityScheduler event=" + event + " state=" + state
                    + " started=" + started + " resumed=" + resumed + " focused=" + focused
                    + " pip=" + activity.isInPictureInPictureMode()
                    + " surface=" + surfaceAttached + " host=" + hostRunning);
        } catch (Exception ex) {
            Log.w(TAG, "ActivityScheduler event unavailable: " + ex.getClass().getSimpleName());
        }
    }

    /** One process-wide, event-driven writer; it holds no Activity or Binder. */
    private static final class EvidenceWriter {
        private static EvidenceWriter instance;
        private final Context context;
        private final Handler handler;
        private final AtomicInteger pending = new AtomicInteger();
        private final AtomicInteger dropped = new AtomicInteger();
        private long sequence;
        static synchronized EvidenceWriter get(Context context) {
            if (instance == null) instance = new EvidenceWriter(context);
            return instance;
        }
        private EvidenceWriter(Context context) {
            this.context = context;
            HandlerThread worker = new HandlerThread("alroot-scheduler-state");
            worker.start();
            handler = new Handler(worker.getLooper());
        }
        void post(JSONObject doc) {
            if (pending.incrementAndGet() > 32) {
                pending.decrementAndGet();
                dropped.incrementAndGet();
                return;
            }
            handler.post(() -> {
                try { write(doc); }
                catch (Exception ex) {
                    Log.w(TAG, "ActivityScheduler evidence unavailable: " + ex.getClass().getSimpleName());
                } finally { pending.decrementAndGet(); }
            });
        }

        private void write(JSONObject doc) throws Exception {
            doc.put("sequence", ++sequence);
            doc.put("sample_wall_ms", System.currentTimeMillis());
            doc.put("sample_elapsed_ns", SystemClock.elapsedRealtimeNanos());
            doc.put("dropped_events", dropped.get());
            try {
                ActivityManager.RunningAppProcessInfo self = new ActivityManager.RunningAppProcessInfo();
                ActivityManager.getMyMemoryState(self);
                doc.put("self_importance", self.importance);
                doc.put("self_importance_reason", self.importanceReasonCode);
            } catch (RuntimeException ex) {
                doc.put("self_importance_error", ex.getClass().getSimpleName());
            }
            ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            JSONArray termux = new JSONArray();
            try {
                List<ActivityManager.RunningAppProcessInfo> processes = am.getRunningAppProcesses();
                if (processes != null) for (ActivityManager.RunningAppProcessInfo process : processes) {
                    if (process.uid != android.os.Process.myUid()
                            || !"com.termux".equals(process.processName)) continue;
                    JSONObject item = new JSONObject();
                    item.put("pid", process.pid);
                    item.put("importance", process.importance);
                    item.put("importance_reason", process.importanceReasonCode);
                    termux.put(item);
                }
            } catch (RuntimeException ex) {
                doc.put("termux_importance_error", ex.getClass().getSimpleName());
            }
            // Empty means unobservable through this public API, not stopped.
            doc.put("observable_termux_processes", termux);
            try {
                PowerManager power = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
                if (power != null) doc.put("interactive", power.isInteractive());
                KeyguardManager keyguard = (KeyguardManager) context.getSystemService(Context.KEYGUARD_SERVICE);
                if (keyguard != null) {
                    doc.put("keyguard_locked", keyguard.isKeyguardLocked());
                    doc.put("device_locked", keyguard.isDeviceLocked());
                }
            } catch (RuntimeException ex) {
                doc.put("power_state_error", ex.getClass().getSimpleName());
            }
            doc.put("self_cgroup", read("/proc/self/cgroup", 4096));
            doc.put("self_cpuset", read("/proc/self/cpuset", 512));
            doc.put("main_thread", threadState(android.os.Process.myPid()));
            int compositor = doc.optInt("compositor_tid");
            if (compositor > 0) doc.put("compositor_thread", threadState(compositor));

            File dir = new File(context.getFilesDir(), "activity-scheduler");
            if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("scheduler directory");
            Os.chmod(dir.getAbsolutePath(), 0700);
            byte[] data = (doc.toString() + "\n").getBytes(StandardCharsets.UTF_8);
            AtomicFile stateFile = new AtomicFile(new File(dir, "state.json"));
            FileOutputStream stateOut = null;
            try {
                stateOut = stateFile.startWrite();
                stateOut.write(data);
                stateFile.finishWrite(stateOut);
            } catch (IOException ex) {
                stateFile.failWrite(stateOut);
                throw ex;
            }
            File log = new File(dir, "events.jsonl");
            if (log.length() + data.length > 128 * 1024) {
                File previous = new File(dir, "events.previous.jsonl");
                if (previous.exists() && !previous.delete()) throw new IOException("rotate old events");
                if (log.exists() && !log.renameTo(previous)) throw new IOException("rotate events");
            }
            try (FileOutputStream out = new FileOutputStream(log, true)) { out.write(data); }
            Os.chmod(log.getAbsolutePath(), 0600);
            Os.chmod(new File(dir, "state.json").getAbsolutePath(), 0600);
        }

        private static JSONObject threadState(int tid) throws Exception {
            JSONObject result = new JSONObject();
            result.put("tid", tid);
            String base = "/proc/self/task/" + tid + "/";
            result.put("cgroup", read(base + "cgroup", 4096));
            result.put("cpuset", read(base + "cpuset", 512));
            String status = read(base + "status", 16384);
            for (String line : status.split("\n")) {
                if (line.startsWith("Cpus_allowed_list:") || line.startsWith("State:")) {
                    int colon = line.indexOf(':');
                    result.put(line.substring(0, colon), line.substring(colon + 1).trim());
                }
            }
            String sched = read(base + "sched", 16384);
            JSONObject scheduling = new JSONObject();
            for (String line : sched.split("\n")) {
                int colon = line.indexOf(':');
                if (colon < 0) continue;
                String key = line.substring(0, colon).trim();
                if (key.equals("policy") || key.equals("prio") || key.contains("uclamp")
                        || key.equals("se.avg.util_avg") || key.equals("se.avg.runnable_avg")
                        || key.equals("se.avg.load_avg") || key.equals("nr_switches"))
                    scheduling.put(key, line.substring(colon + 1).trim());
            }
            result.put("sched", scheduling);
            String stat = read(base + "stat", 4096);
            int end = stat.lastIndexOf(')');
            if (end > 0) {
                String[] fields = stat.substring(end + 1).trim().split("\\s+");
                if (fields.length > 38) {
                    result.put("ppid", fields[1]);
                    result.put("priority", fields[15]);
                    result.put("nice", fields[16]);
                    result.put("start_ticks", fields[19]);
                    result.put("last_cpu", fields[36]);
                    result.put("rt_priority", fields[37]);
                    result.put("policy", fields[38]);
                }
            }
            if (status.startsWith("unavailable:")) result.put("status_error", status);
            if (sched.startsWith("unavailable:")) result.put("sched_error", sched);
            if (stat.startsWith("unavailable:")) result.put("stat_error", stat);
            return result;
        }

        private static String read(String path, int limit) {
            try (FileInputStream in = new FileInputStream(path)) {
                byte[] bytes = new byte[limit + 1];
                int size = 0, count;
                while (size < bytes.length && (count = in.read(bytes, size, bytes.length - size)) > 0)
                    size += count;
                String value = new String(bytes, 0, Math.min(size, limit), StandardCharsets.UTF_8).trim();
                return size > limit ? value + "\n[truncated]" : value;
            } catch (Exception ex) { return "unavailable:" + ex.getClass().getSimpleName(); }
        }
    }
}
