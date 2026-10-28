package com.alroot.wayland;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PictureInPictureParams;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.Configuration;
import android.content.pm.PackageManager;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.util.Rational;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Choreographer;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.PixelCopy;
import android.view.PointerIcon;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;

public final class MainActivity extends Activity implements SurfaceHolder.Callback {
    private final Handler main = new Handler(Looper.getMainLooper());
    private InputSurfaceView view;
    private DisplayTiming displayTiming;
    private CursorOverlay cursorOverlay;
    private LinearLayout overlayRoot;
    private Button menuHandle;
    private TextView status;
    private boolean overlayWasVisibleBeforePip;
    private boolean pipTransitionInProgress;
    private boolean pipHostReadyPreviously;
    private Surface attached;
    private static Surface activeSurface;
    private boolean suppressEscape;
    private boolean vsyncOn;
    private String clipboardEcho;
    private ClipboardManager clipboard;
    private ClipboardManager.OnPrimaryClipChangedListener clipboardObserver;
    private WaylandCompositor.ClipboardListener guestClipboardListener;
    private boolean surfaceProbeInFlight;
    private final Runnable surfaceProbe = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed()) return;
            if (HelperService.running && hasWindowFocus() && view != null
                    && view.isShown() && view.getHolder().getSurface().isValid()
                    && !surfaceProbeInFlight) {
                surfaceProbeInFlight = true;
                final Bitmap sample = Bitmap.createBitmap(160, 90, Bitmap.Config.ARGB_8888);
                try {
                    // PixelCopy reads our own Android SurfaceView, not the
                    // guest X framebuffer or a privileged device screenshot.
                    PixelCopy.request(view, sample, result -> {
                        surfaceProbeInFlight = false;
                        if (result == PixelCopy.SUCCESS) {
                            int nonblack = 0, minBrightness = 765, maxBrightness = 0;
                            int total = sample.getWidth() * sample.getHeight();
                            for (int y = 0; y < sample.getHeight(); y++) {
                                for (int x = 0; x < sample.getWidth(); x++) {
                                    int rgba = sample.getPixel(x, y);
                                    int brightness = ((rgba >> 16) & 255)
                                            + ((rgba >> 8) & 255) + (rgba & 255);
                                    if (brightness > 36) nonblack++;
                                    if (brightness < minBrightness) minBrightness = brightness;
                                    if (brightness > maxBrightness) maxBrightness = brightness;
                                }
                            }
                            android.util.Log.i("AlrootWayland", "PixelProbe SurfaceView"
                                    + " copy=SUCCESS samples=" + total
                                    + " nonblack=" + nonblack
                                    + " brightness-range=" + minBrightness + ".." + maxBrightness
                                    + " (not an assertion that the desktop is correctly displayed)");
                        } else {
                            android.util.Log.w("AlrootWayland",
                                    "PixelProbe SurfaceView copy failed, result=" + result);
                        }
                        sample.recycle();
                    }, main);
                } catch (Exception e) {
                    surfaceProbeInFlight = false;
                    sample.recycle();
                    android.util.Log.w("AlrootWayland", "PixelProbe unavailable", e);
                }
            }
            main.postDelayed(this, 12000);
        }
    };
    private final int[] cursorSnapshot = new int[WaylandCompositor.CURSOR_BUF_INTS];
    private int lastCursorSerial = Integer.MIN_VALUE;
    private final Runnable cursorPoll = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed()) return;
            if (HelperService.running && cursorOverlay != null) {
                try {
                    int serial = WaylandCompositor.nativeCursorSerial();
                    if (serial != lastCursorSerial) {
                        int length = WaylandCompositor.nativeCursorSnapshot(cursorSnapshot);
                        if (length >= 6) {
                            boolean hidden = cursorSnapshot[1] != 0 && serial > 0;
                            int w = cursorSnapshot[2], h = cursorSnapshot[3];
                            Bitmap bitmap = null;
                            if (!hidden && w > 0 && h > 0 && w <= 256 && h <= 256
                                    && length >= 6 + w * h) {
                                bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                                bitmap.setPixels(cursorSnapshot, 6, w, 0, 0, w, h);
                                bitmap.setDensity(Bitmap.DENSITY_NONE);
                            }
                            view.setGuestCursor(bitmap, cursorSnapshot[4], cursorSnapshot[5], hidden);
                            cursorOverlay.setGuestCursor(bitmap, cursorSnapshot[4], cursorSnapshot[5], hidden);
                        }
                        lastCursorSerial = serial;
                    }
                } catch (Throwable ex) {
                    android.util.Log.w("AlrootWayland", "cursor snapshot unavailable", ex);
                }
            }
            main.postDelayed(this, 150);
        }
    };
    private final Runnable clipboardPoll = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed()) return;
            if (hasWindowFocus() && HelperService.running) {
                String text = ClipboardBridge.updatedX11Text();
                if (text != null) {
                    WaylandCompositor.setClipboardText(text);
                    if (clipboard != null) {
                        CharSequence old = clipboard.hasPrimaryClip()
                                ? clipboard.getPrimaryClip().getItemAt(0).coerceToText(MainActivity.this) : "";
                        if (!text.contentEquals(old)) {
                            clipboardEcho = text;
                            clipboard.setPrimaryClip(ClipData.newPlainText("Linux X11", text));
                        }
                    }
                }
            }
            main.postDelayed(this, 450);
        }
    };

    private final Choreographer.FrameCallback vsync = new Choreographer.FrameCallback() {
        @Override public void doFrame(long frameTimeNanos) {
            if (!vsyncOn) return;
            if (displayTiming != null) displayTiming.frame(frameTimeNanos);
            if (HelperService.running)
                WaylandCompositor.nativeVsync(frameTimeNanos);
            Choreographer.getInstance().postFrameCallback(this);
        }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        view = new InputSurfaceView(this);
        view.getHolder().addCallback(this);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        // The scene does not directly composite ROLE_CURSOR. Keep Android's
        // sole mouse pointer visible until a touchpad gesture chooses the
        // Alroot software overlay. Do not hide both and lose the cursor.
        root.addView(view, new FrameLayout.LayoutParams(-1, -1));
        cursorOverlay = new CursorOverlay(this, view);
        // Draw on top without stealing the mouse/touch events from SurfaceView.
        cursorOverlay.setEnabled(false);
        root.addView(cursorOverlay, new FrameLayout.LayoutParams(-1, -1));
        view.setCursorMoveListener(() -> cursorOverlay.invalidate());
        overlayRoot = new LinearLayout(this);
        overlayRoot.setOrientation(LinearLayout.VERTICAL);
        overlayRoot.setGravity(Gravity.START);
        menuHandle = new Button(this);
        menuHandle.setText("☰");
        menuHandle.setTextSize(16);
        menuHandle.setTextColor(Color.WHITE);
        menuHandle.setAllCaps(false);
        menuHandle.setMinWidth(0);
        menuHandle.setMinimumWidth(0);
        menuHandle.setMinHeight(0);
        menuHandle.setMinimumHeight(0);
        GradientDrawable pill = new GradientDrawable();
        pill.setColor(0xB6222830);
        pill.setCornerRadius(dp(11));
        menuHandle.setBackground(pill);
        overlayRoot.addView(menuHandle, new LinearLayout.LayoutParams(dp(44), dp(40)));
        menuHandle.setOnClickListener(v -> showMenu(menuHandle));
        menuHandle.setOnLongClickListener(v -> { toggleStatusOverlay(); return true; });
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(10);
        status.setMaxWidth(dp(235));
        status.setPadding(dp(8), dp(5), dp(8), dp(5));
        status.setBackgroundColor(0xB6222830);
        status.setVisibility(View.GONE);
        overlayRoot.addView(status, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                -2, -2, Gravity.TOP | Gravity.START);
        params.leftMargin = dp(6);
        params.topMargin = dp(6);
        root.addView(overlayRoot, params);
        view.setOverlayToggle(() -> {
            if (overlayRoot.getVisibility() == View.VISIBLE) {
                overlayRoot.setVisibility(View.GONE);
            } else {
                overlayRoot.setVisibility(View.VISIBLE);
            }
        });
        setContentView(root);
        displayTiming = new DisplayTiming(this);

        clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        clipboardObserver = () -> {
            if (!hasWindowFocus() || clipboard == null || !clipboard.hasPrimaryClip()) return;
            CharSequence text = clipboard.getPrimaryClip().getItemAt(0).coerceToText(this);
            String s = text == null ? "" : text.toString();
            if (s.equals(clipboardEcho)) { clipboardEcho = null; return; }
            ClipboardBridge.androidToX11(s);
            WaylandCompositor.setClipboardText(s);
        };
        clipboard.addPrimaryClipChangedListener(clipboardObserver);
        guestClipboardListener = text -> runOnUiThread(() -> {
            if (clipboard == null) return;
            CharSequence old = clipboard.hasPrimaryClip()
                    ? clipboard.getPrimaryClip().getItemAt(0).coerceToText(this) : "";
            if (text.contentEquals(old)) return;
            ClipboardBridge.androidToX11(text);
            clipboardEcho = text;
            clipboard.setPrimaryClip(ClipData.newPlainText("Linux", text));
        });
        WaylandCompositor.setClipboardListener(guestClipboardListener);

        if (Build.VERSION.SDK_INT >= 33)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::toggleIme);

        restoreWindowSettings();
        refreshPiPParams();
        startForegroundService(new Intent(this, HelperService.class));
        main.post(cursorPoll);
        main.post(clipboardPoll);
        main.postDelayed(surfaceProbe, 8000);
        updateStatus();
        main.postDelayed(new Runnable() {
            @Override public void run() {
                if (isFinishing() || isDestroyed()) return;
                attachIfReady();
                if (pipHostReadyPreviously != HelperService.running) {
                    pipHostReadyPreviously = HelperService.running;
                    refreshPiPParams();
                }
                LaunchInbox.consume(MainActivity.this);
                updateStatus();
                main.postDelayed(this, 1800);
            }
        }, 200);
    }

    private void updateStatus() {
        if (status == null) return;
        boolean socket = new File(HelperService.runtimeDir(), "wayland-0").exists();
        status.setText(TermuxSession.uidAndSigning(this)
                + "\n" + getString(R.string.status_host, socket ? "SOCKET" : "STARTING")
                + " | " + getString(view.isCaptured() ? R.string.status_mouse_captured :
                                                R.string.status_mouse_free)
                + (HelperService.error != null ? "\n" + HelperService.error : ""));
    }
    private int dp(float size) {
        return Math.round(size * getResources().getDisplayMetrics().density);
    }
    private void toggleStatusOverlay() {
        status.setVisibility(status.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE);
    }

    private void toast(String msg) {
        Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
        android.util.Log.i("AlrootWayland", msg);
    }

    private void showMenu(View anchor) {
        PopupMenu pop = new PopupMenu(this, anchor);
        final int[] labels = {R.string.menu_start_xfce, R.string.menu_start_direct,
                R.string.menu_pointer_capture, R.string.menu_keyboard,
                R.string.menu_resolution, R.string.menu_scale, R.string.menu_orientation,
                R.string.menu_fullscreen, R.string.menu_keep_screen, R.string.menu_status,
                R.string.menu_logs, R.string.menu_stop_session, R.string.menu_stop_host,
                R.string.menu_input_mode, R.string.menu_pointer_speed,
                R.string.menu_pointer_accel, R.string.menu_toggle_status,
                R.string.menu_hide_handle, R.string.menu_pip, R.string.menu_auto_pip};
        String[] items = new String[labels.length];
        for (int i = 0; i < labels.length; i++) items[i] = getString(labels[i]);
        for (int i = 0; i < items.length; i++) pop.getMenu().add(0, i, i, items[i]);
        pop.setOnMenuItemClickListener(item -> {
            switch (item.getItemId()) {
                case 0: toast(TermuxSession.launch("start", "xfce-labwc")); break;
                case 1: toast(TermuxSession.launch("start", "xfce-shm-direct")); break;
                case 2: view.setCapture(!view.isCaptured()); break;
                case 3: toggleIme(); break;
                case 4: chooseResolution(); break;
                case 5: chooseScale(); break;
                case 6: rotateMode(); break;
                case 7: toggleFullscreen(); break;
                case 8: toggleScreenOn(); break;
                case 9: toast(TermuxSession.launch("status", "")); break;
                case 10: toast(getString(R.string.toast_log_path));
                    break;
                case 11: confirmStop(false); break;
                case 12: confirmStop(true); break;
                case 13: chooseInputMode(); break;
                case 14: choosePointerSetting(false); break;
                case 15: choosePointerSetting(true); break;
                case 16: toggleStatusOverlay(); break;
                case 17:
                    status.setVisibility(View.GONE);
                    overlayRoot.setVisibility(View.GONE);
                    toast(getString(R.string.toast_handle_hidden));
                    break;
                case 18: enterPiP(false); break;
                case 19: toggleAutomaticPiP(); break;
                default: break;
            }
            updateStatus();
            return true;
        });
        pop.show();
    }
    private void chooseInputMode() {
        final String[] modes = {getString(R.string.mode_touchpad),
                getString(R.string.mode_virtual_touch), getString(R.string.mode_direct_touch)};
        new AlertDialog.Builder(this).setTitle(R.string.title_input_mode)
                .setSingleChoiceItems(modes, view.getInputMode(), (dialog, selected) -> {
                    view.configurePointer(selected, view.getPointerSpeed(), view.getAcceleration());
                    dialog.dismiss();
                    toast(getString(R.string.toast_input_mode, modes[selected]));
                }).setNegativeButton(R.string.action_cancel, null).show();
    }
    private void choosePointerSetting(boolean accelerationSetting) {
        int min = accelerationSetting ? 0 : 50;
        int max = accelerationSetting ? 200 : 300;
        int initial = accelerationSetting ? view.getAcceleration() : view.getPointerSpeed();
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(dp(16), dp(8), dp(16), dp(4));
        TextView value = new TextView(this);
        value.setText(String.valueOf(initial));
        SeekBar slider = new SeekBar(this);
        slider.setMax(max - min);
        slider.setProgress(initial - min);
        layout.addView(value);
        layout.addView(slider);
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                value.setText(String.valueOf(progress + min));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {}
        });
        new AlertDialog.Builder(this)
                .setTitle(accelerationSetting ? R.string.title_pointer_accel : R.string.title_pointer_speed)
                .setView(layout).setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.action_apply, (d, n) -> {
                    int target = slider.getProgress() + min;
                    view.configurePointer(view.getInputMode(),
                            accelerationSetting ? view.getPointerSpeed() : target,
                            accelerationSetting ? target : view.getAcceleration());
                }).show();
    }

    private void confirmStop(boolean server) {
        new AlertDialog.Builder(this)
                .setTitle(server ? R.string.title_stop_host : R.string.title_stop_session)
                .setMessage(R.string.msg_stop_session)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.action_stop, (d, which) -> {
                    toast(TermuxSession.launch("stop", ""));
                    if (server) {
                        view.setCapture(false);
                        startService(new Intent(this, HelperService.class)
                                .setAction(HelperService.ACTION_STOP));
                    }
                }).show();
    }

    private void chooseResolution() {
        EditText input = new EditText(this);
        int w = prefs().getInt("width", 1920), h = prefs().getInt("height", 1080);
        input.setSingleLine(true);
        input.setText(w + "x" + h);
        new AlertDialog.Builder(this).setTitle(R.string.title_resolution)
                .setView(input).setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.action_apply, (d, n) -> {
                    try {
                        String[] pair = input.getText().toString().trim().toLowerCase().split("x");
                        int width = Integer.parseInt(pair[0]), height = Integer.parseInt(pair[1]);
                        if (width < 320 || width > 3840 || height < 240 || height > 2160)
                            throw new IllegalArgumentException("outside validated range");
                        prefs().edit().putInt("width", width).putInt("height", height).apply();
                        SessionFiles.setResolution(width, height);
                        WaylandCompositor.nativeResizeOutput(width, height);
                        applyScale();
                        refreshPiPParams();
                        toast(getString(R.string.toast_resolution_applied, width, height));
                    } catch (Exception e) { toast(getString(R.string.toast_bad_resolution)); }
                }).show();
    }

    private void chooseScale() {
        String[] names = {getString(R.string.mode_fit),
                getString(R.string.mode_stretch), getString(R.string.mode_fill)};
        int[] values = {1, 2, 3};
        new AlertDialog.Builder(this).setTitle(R.string.title_scale).setItems(names, (d, index) -> {
            prefs().edit().putInt("mode", values[index]).apply();
            applyScale();
            if (prefs().getBoolean("fullscreen", true))
                toast(getString(R.string.toast_fullscreen_fit));
        }).show();
    }

    private void applyScale() {
        // Never stretch the XFCE desktop. 1920x1080 must remain 16:9/96 DPI.
        // On a wider phone, FIT fills the entire height, with side margins.
        int mode = prefs().getBoolean("fullscreen", true) ? 1 : prefs().getInt("mode", 1);
        WaylandCompositor.nativeSetScaleMode(mode, 0);
        view.updateTransform(prefs().getInt("width", 1920),
                prefs().getInt("height", 1080), mode);
    }

    private void rotateMode() {
        int orientation = prefs().getInt("orientation", 0);
        orientation = (orientation + 1) % 3;
        prefs().edit().putInt("orientation", orientation).apply();
        setRequestedOrientation(orientation == 1 ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                : orientation == 2 ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                : ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
    }

    private void toggleFullscreen() {
        prefs().edit().putBoolean("fullscreen", !prefs().getBoolean("fullscreen", true)).apply();
        restoreWindowSettings();
    }

    private void toggleScreenOn() {
        prefs().edit().putBoolean("keep_screen", !prefs().getBoolean("keep_screen", true)).apply();
        restoreWindowSettings();
    }

    private boolean pipSupported() {
        return Build.VERSION.SDK_INT >= 26 &&
                getPackageManager().hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE);
    }

    private PictureInPictureParams makePipParams(boolean autoEnter) {
        int w = Math.max(320, prefs().getInt("width", 1920));
        int h = Math.max(240, prefs().getInt("height", 1080));
        PictureInPictureParams.Builder params = new PictureInPictureParams.Builder()
                .setAspectRatio(new Rational(w, h));
        if (Build.VERSION.SDK_INT >= 31) {
            params.setAutoEnterEnabled(autoEnter);
            params.setSeamlessResizeEnabled(true);
        }
        return params.build();
    }

    private void refreshPiPParams() {
        if (!pipSupported()) return;
        try {
            setPictureInPictureParams(makePipParams(
                    prefs().getBoolean("pip_auto", true) && HelperService.running));
        } catch (Exception e) {
            android.util.Log.w("AlrootWayland", "Cannot configure PiP parameters", e);
        }
    }

    private void enterPiP(boolean automatic) {
        if (!pipSupported() || isInPictureInPictureMode()) {
            if (!automatic) toast(getString(R.string.toast_pip_unavailable));
            return;
        }
        try {
            view.releaseAllKeys();
            view.releasePointerCapture();
            InputMethodManager imm = (InputMethodManager)
                    getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
            pipTransitionInProgress = true;
            boolean accepted = enterPictureInPictureMode(makePipParams(false));
            if (!accepted && !automatic) toast(getString(R.string.toast_pip_unavailable));
            android.util.Log.i("AlrootWayland", "PiP request automatic=" + automatic
                    + " accepted=" + accepted + " hostRunning=" + HelperService.running);
        } catch (Exception e) {
            android.util.Log.w("AlrootWayland", "PiP entry rejected by Android", e);
            if (!automatic) toast(getString(R.string.toast_pip_unavailable));
        } finally {
            pipTransitionInProgress = false;
        }
    }

    private void toggleAutomaticPiP() {
        boolean enabled = !prefs().getBoolean("pip_auto", true);
        prefs().edit().putBoolean("pip_auto", enabled).apply();
        refreshPiPParams();
        toast(getString(enabled ? R.string.toast_auto_pip_on : R.string.toast_auto_pip_off));
    }

    @Override protected void onUserLeaveHint() {
        super.onUserLeaveHint();
        // Android 12+ honors PictureInPictureParams.setAutoEnterEnabled.
        // Pre-S (API 26-30) must explicitly enter when Home is pressed.
        if (Build.VERSION.SDK_INT < 31 && !pipTransitionInProgress &&
                prefs().getBoolean("pip_auto", true) && HelperService.running)
            enterPiP(true);
    }

    @Override public void onPictureInPictureModeChanged(
            boolean inPictureInPictureMode, Configuration newConfig) {
        super.onPictureInPictureModeChanged(inPictureInPictureMode, newConfig);
        if (inPictureInPictureMode) {
            overlayWasVisibleBeforePip = overlayRoot != null &&
                    overlayRoot.getVisibility() == View.VISIBLE;
            if (overlayRoot != null) overlayRoot.setVisibility(View.GONE);
            if (view != null) {
                view.releaseAllKeys();
                view.releasePointerCapture();
            }
        } else {
            if (overlayRoot != null && overlayWasVisibleBeforePip)
                overlayRoot.setVisibility(View.VISIBLE);
            restoreWindowSettings();
            refreshPiPParams();
        }
        main.post(this::applyScale);
        android.util.Log.i("AlrootWayland", "PiP changed active=" + inPictureInPictureMode
                + " compositor service retained=" + HelperService.running);
    }

    private android.content.SharedPreferences prefs() {
        return getSharedPreferences("display", MODE_PRIVATE);
    }

    private void restoreWindowSettings() {
        if (Build.VERSION.SDK_INT >= 26 && isInPictureInPictureMode()) return;
        if (prefs().getBoolean("keep_screen", true))
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        boolean fullscreen = prefs().getBoolean("fullscreen", true);
        if (fullscreen) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            if (Build.VERSION.SDK_INT >= 28) {
                WindowManager.LayoutParams attributes = getWindow().getAttributes();
                attributes.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                getWindow().setAttributes(attributes);
            }
        } else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(!fullscreen);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                if (fullscreen) {
                    controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                } else {
                    controller.show(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                }
            }
        }
        getWindow().getDecorView().setSystemUiVisibility(fullscreen
                ? View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE : 0);
        if (view != null) applyScale();
        int o = prefs().getInt("orientation", 0);
        setRequestedOrientation(o == 1 ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                : o == 2 ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                : ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
    }

    private void toggleIme() {
        InputMethodManager im = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        boolean visible = Build.VERSION.SDK_INT >= 30 && view.getRootWindowInsets() != null
                && view.getRootWindowInsets().isVisible(WindowInsets.Type.ime());
        if (visible) {
            // Samsung IME may hide without sending a final nonempty commit;
            // finalize the last buffered Hangul syllable before dismissal.
            ImeBridge.finishComposing();
            im.hideSoftInputFromWindow(view.getWindowToken(), 0);
        }
        else {
            view.requestFocus();
            ImeBridge.resetComposing();
            view.post(() -> {
                im.restartInput(view);
                boolean shown = im.showSoftInput(view, InputMethodManager.SHOW_IMPLICIT);
                android.util.Log.i("AlrootWayland", "IME showSoftInput requested=" + shown
                        + " viewFocused=" + view.hasFocus());
            });
        }
    }

    @Override public void onBackPressed() { toggleIme(); }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_M &&
                event.isCtrlPressed() && event.isAltPressed()) {
            if (event.getAction() == KeyEvent.ACTION_DOWN &&
                    event.getRepeatCount() == 0 && overlayRoot != null)
                overlayRoot.setVisibility(overlayRoot.getVisibility() == View.VISIBLE
                        ? View.GONE : View.VISIBLE);
            return true;
        }
        if (event.getKeyCode() == KeyEvent.KEYCODE_ESCAPE) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.isCtrlPressed() && event.isAltPressed()) {
                suppressEscape = true;
                view.releaseAllKeys();
                view.setCapture(false);
                updateStatus();
                return true;
            }
            if (suppressEscape) {
                if (event.getAction() == KeyEvent.ACTION_UP) suppressEscape = false;
                return true;
            }
        }
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) return super.dispatchKeyEvent(event);
        return view.dispatchKeyEvent(event) || super.dispatchKeyEvent(event);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        attached = holder.getSurface();
        if (displayTiming != null) displayTiming.configureSurface(attached);
        attachIfReady();
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        attached = holder.getSurface();
        if (displayTiming != null) {
            displayTiming.refreshDisplay();
            displayTiming.configureSurface(attached);
        }
        attachIfReady();
        applyScale();
    }

    private void attachIfReady() {
        if (attached == null || !attached.isValid() || !HelperService.running) return;
        if (activeSurface != attached) {
            WaylandCompositor.nativeSetSurface(attached);
            activeSurface = attached;
            if (displayTiming != null) {
                displayTiming.refreshDisplay();
                displayTiming.configureSurface(attached);
            }
            applyScale();
        }
        if (!vsyncOn) {
            vsyncOn = true;
            Choreographer.getInstance().postFrameCallback(vsync);
        }
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        detach();
        attached = null;
    }

    private void detach() {
        vsyncOn = false;
        Choreographer.getInstance().removeFrameCallback(vsync);
        if (activeSurface == attached && HelperService.running) {
            WaylandCompositor.nativeSetSurface(null);
            activeSurface = null;
        }
        view.releaseAllKeys();
        view.releasePointerCapture();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && prefs().getBoolean("fullscreen", true))
            restoreWindowSettings();
        if (!hasFocus && view != null) {
            ImeBridge.finishComposing();
            view.releaseAllKeys();
            view.releasePointerCapture();
        } else if (hasFocus && clipboardObserver != null) {
            // Android restricts clipboard access in the background; resync on focus.
            clipboardObserver.onPrimaryClipChanged();
        }
        updateStatus();
    }

    @Override protected void onDestroy() {
        if (displayTiming != null) displayTiming.close();
        main.removeCallbacks(cursorPoll);
        main.removeCallbacks(clipboardPoll);
        main.removeCallbacks(surfaceProbe);
        detach();
        if (clipboard != null && clipboardObserver != null)
            clipboard.removePrimaryClipChangedListener(clipboardObserver);
        WaylandCompositor.clearClipboardListener(guestClipboardListener);
        super.onDestroy();
    }
}
