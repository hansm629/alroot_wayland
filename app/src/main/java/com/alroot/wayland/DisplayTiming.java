package com.alroot.wayland;

import android.app.Activity;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import com.droiddeck.launcher.wayland.WaylandCompositor;

/**
 * Synchronizes the actual Android display/vsync cadence with wl_output mode.
 * A mode value without a running Choreographer at that cadence is not enough.
 */
public final class DisplayTiming implements DisplayManager.DisplayListener {
    private static final String TAG = "AlrootWayland";
    private final Activity activity;
    private final DisplayManager manager;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean registered;
    private float physicalRateHz = 60f;
    private float outputRateHz = -1f;
    private long windowFirstNs;
    private long lastFrameNs;
    private int samples;
    private long lastLogMs;

    public DisplayTiming(Activity activity) {
        this.activity = activity;
        this.manager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (manager != null) {
            manager.registerDisplayListener(this, handler);
            registered = true;
        }
        refreshDisplay();
    }

    private Display display() {
        Display attached = activity.getDisplay();
        if (attached != null) return attached;
        return manager != null ? manager.getDisplay(Display.DEFAULT_DISPLAY) : null;
    }

    private static boolean valid(float hz) { return hz >= 30f && hz <= 240f; }

    public void refreshDisplay() {
        Display d = display();
        if (d == null) return;
        float active = d.getRefreshRate();
        if (!valid(active)) return;
        physicalRateHz = active;
        Log.i(TAG, "DisplayTiming active display=" + d.getDisplayId()
                + " physical-mode-Hz=" + active
                + " modeId=" + d.getMode().getModeId()
                + " resolution=" + d.getMode().getPhysicalWidth()
                + "x" + d.getMode().getPhysicalHeight());
        // Keep the native mode matched to the currently selected physical
        // Android mode. Once enough real VSync samples exist, adjust any
        // discrepancy using Choreographer timestamps, not a fixed 120Hz.
        advertise(active, "active-display");
    }

    public void configureSurface(Surface surface) {
        if (surface == null || !surface.isValid() || Build.VERSION.SDK_INT < 30) return;
        try {
            if (valid(physicalRateHz)) {
                surface.setFrameRate(physicalRateHz,
                        Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                        Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
                Log.i(TAG, "DisplayTiming Android Surface requested "
                        + physicalRateHz + "Hz fixed-source seamless");
            }
        } catch (Throwable e) {
            Log.w(TAG, "DisplayTiming Surface frame-rate request not accepted", e);
        }
    }

    private void advertise(float rate, String evidence) {
        if (!valid(rate) || !HelperService.running) return;
        if (outputRateHz > 0 && Math.abs(outputRateHz - rate) < 1f) return;
        outputRateHz = rate;
        WaylandCompositor.nativeSetOutputRefreshRate(rate);
        Log.i(TAG, "DisplayTiming wl_output=" + rate + "Hz source=" + evidence
                + " (Choreographer is the real presentation tick source)");
    }

    public void frame(long frameTimeNanos) {
        if (lastFrameNs == 0 || frameTimeNanos <= lastFrameNs ||
                frameTimeNanos - lastFrameNs > 100_000_000L) {
            windowFirstNs = frameTimeNanos;
            samples = 0;
        } else if (frameTimeNanos - lastFrameNs >= 3_000_000L &&
                frameTimeNanos - lastFrameNs <= 40_000_000L) {
            if (samples++ == 0) windowFirstNs = lastFrameNs;
        } else {
            windowFirstNs = frameTimeNanos;
            samples = 0;
        }
        lastFrameNs = frameTimeNanos;
        if (samples < 90 || frameTimeNanos <= windowFirstNs) return;
        float actual = (float)((double)samples * 1_000_000_000d /
                (double)(frameTimeNanos - windowFirstNs));
        samples = 0;
        windowFirstNs = frameTimeNanos;
        if (!valid(actual)) return;
        // The physical Display.getRefreshRate() / active mode is the
        // authority for wl_output refresh. Choreographer callback delivery
        // can contain jitter or batched timestamps and must NOT promote a
        // transient 122Hz measurement into a fake mode on a 120Hz panel.
        // Dynamic physical mode switches arrive through DisplayListener.
        if (Math.abs(actual - physicalRateHz) > 4f && lastLogMs > 0) {
            Log.w(TAG, "DisplayTiming measured VSync differs from active panel rate"
                    + " measured=" + actual + " active=" + physicalRateHz
                    + " (keeping physical mode)");
        }
        advertise(physicalRateHz, "active-display+measured-vsync");
        long now = android.os.SystemClock.elapsedRealtime();
        if (now - lastLogMs > 2500) {
            lastLogMs = now;
            Log.i(TAG, "DisplayTiming measured-vsync-Hz=" + actual
                    + " physical-active-Hz=" + physicalRateHz
                    + " advertised-Hz=" + outputRateHz);
        }
    }

    @Override public void onDisplayAdded(int id) { refreshDisplay(); }
    @Override public void onDisplayRemoved(int id) { refreshDisplay(); }
    @Override public void onDisplayChanged(int id) {
        Display d = display();
        if (d != null && d.getDisplayId() == id) refreshDisplay();
    }

    public void close() {
        if (manager != null && registered) {
            manager.unregisterDisplayListener(this);
            registered = false;
        }
    }
}
