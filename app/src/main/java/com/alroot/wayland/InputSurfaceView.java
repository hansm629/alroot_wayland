package com.alroot.wayland;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.SurroundingText;
import java.util.HashSet;
import java.util.Set;

public final class InputSurfaceView extends SurfaceView {
    public static final int MODE_TOUCHPAD = 0;
    public static final int MODE_VIRTUAL_TOUCHSCREEN = 1;
    public static final int MODE_DIRECT_TOUCH = 2;
    private final Set<Integer> keys = new HashSet<>();
    // Termux-X11 keeps one InputConnection throughout a view's lifetime.
    // Samsung can request it twice on first focus; two independent fake
    // cursors let the previous English preedit leak into the new connection.
    private final ImeConnectionHolder<InputConnection> imeConnections =
            new ImeConnectionHolder<>();
    // Virtual uppercase key-downs are sent as complete scoped keystrokes;
    // their optional later key-up must not release an unrelated guest key.
    private final Set<Integer> syntheticShiftKeyUps = new HashSet<>();
    private final Handler gestures = new Handler(Looper.getMainLooper());
    private final float touchSlop;
    private final TouchpadScrollState scrollState;
    private int inputMode;
    private int pointerSpeed;
    private int acceleration;
    private int primaryPointer = -1;
    private boolean touchpadCursorActive;
    private long touchStarted;
    private float touchStartX, touchStartY, prevX, prevY, prevCentroidY;
    private float scrollResidualY;
    private boolean twoFingerScrollStarted;
    private boolean pressed, dragStarted, touchMoved, multipleTouch, scrolled, overlayGesture;
    private Runnable overlayToggle;
    private final Runnable longPress = () -> {
        if (inputMode == MODE_TOUCHPAD && primaryPointer >= 0
                && !touchMoved && !multipleTouch && !overlayGesture && !pressed) {
            sendButton(272, true);
            pressed = true;
            dragStarted = true;
        }
    };
    private boolean captured;
    // Android can dispatch a physical mouse click as BOTH ACTION_BUTTON_PRESS
    // (generic motion) and ACTION_DOWN (touch), or without a preceding hover.
    // Track forwarded buttons to prevent duplicate X11 clicks/drag state.
    private int mouseButtonMask;
    private float lastX, lastY;
    private boolean pointerInitialized;
    private boolean pointerViewSized;
    private float pointerScaleX = 1, pointerScaleY = 1;
    private int imeEventCount;
    private int imeCharacterCount;
    private int imeFirstEditorDiagnostics;
    private final RectF picture = new RectF();
    private PointerIcon guestIcon;
    private boolean guestCursorHidden;
    private Runnable onCursorMove;

    public InputSurfaceView(Context context) {
        super(context);
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        scrollState = new TouchpadScrollState(
                touchSlop, getResources().getDisplayMetrics().density);
        SharedPreferences p = context.getSharedPreferences("display", Context.MODE_PRIVATE);
        inputMode = p.getInt("input_mode", MODE_TOUCHPAD);
        // P18 forced the project overlay on before the first touch. Android's
        // existing mouse cursor was still visible, resulting in two cursors
        // (one at the old Android coordinate, one at the scene center).
        // Use hardware pointer alone until an intentional touchpad gesture.
        touchpadCursorActive = false;
        pointerSpeed = p.getInt("pointer_speed", 190);
        acceleration = p.getInt("pointer_accel", 110);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setClickable(true);
        setOnClickListener(v -> requestFocus());
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (w <= 0 || h <= 0) return;
        SharedPreferences prefs = getContext().getSharedPreferences(
                "display", Context.MODE_PRIVATE);
        updateTransform(prefs.getInt("width", 1920), prefs.getInt("height", 1080),
                prefs.getBoolean("fullscreen", true) ? 1 : prefs.getInt("mode", 1));
        if (!pointerInitialized) {
            // The native host starts at the scene center. Match its cursor
            // immediately, before the first touch or pointer-capture event;
            // never send a late synthetic motion when XFCE becomes ready.
            lastX = PointerStartupPolicy.midpoint(picture.left, picture.right);
            lastY = PointerStartupPolicy.midpoint(picture.top, picture.bottom);
            // Display positioning is not a guest input event. Keep the
            // first real touch/capture responsible for synchronizing with
            // the native pointer if this Activity reattached an old server.
        }
        pointerViewSized = true;
        if (oldw == 0 && oldh == 0)
            android.util.Log.i("AlrootWayland", "P18 pointer view ready, " +
                    "visual-center=" + (int)lastX + "," + (int)lastY +
                    " touchpad=" + (inputMode == MODE_TOUCHPAD) +
                    " Android-cursor-hidden=" + (inputMode == MODE_TOUCHPAD));
        if (onCursorMove != null) onCursorMove.run();
    }

    public boolean isCaptured() { return captured; }
    public float pointerX() { return lastX; }
    public float pointerY() { return lastY; }
    public boolean isSoftwareCursorWanted() {
        return PointerStartupPolicy.showSoftwareCursor(inputMode, captured,
                touchpadCursorActive && guestIcon != null, pointerViewSized);
    }
    public float cursorScaleX() { return 1f / pointerScaleX; }
    public float cursorScaleY() { return 1f / pointerScaleY; }
    public void setCursorMoveListener(Runnable listener) { onCursorMove = listener; }
    public void setOverlayToggle(Runnable action) { overlayToggle = action; }
    public int getInputMode() { return inputMode; }
    public int getPointerSpeed() { return pointerSpeed; }
    public int getAcceleration() { return acceleration; }
    public void configurePointer(int mode, int speed, int accel) {
        if (mode < MODE_TOUCHPAD || mode > MODE_DIRECT_TOUCH) return;
        cancelGesture();
        inputMode = mode;
        touchpadCursorActive = false;
        pointerSpeed = Math.max(50, Math.min(300, speed));
        acceleration = Math.max(0, Math.min(200, accel));
        getContext().getSharedPreferences("display", Context.MODE_PRIVATE).edit()
                .putInt("input_mode", inputMode)
                .putInt("pointer_speed", pointerSpeed)
                .putInt("pointer_accel", acceleration).apply();
        invalidate();
        refreshPointerOwnership();
        if (onCursorMove != null) onCursorMove.run();
    }
    private void sendButton(int evdev, boolean down) {
        WaylandCompositor.nativeSendSceneInput(3, evdev, down ? 1 : 0);
    }
    private void sendHardwareMouseButton(int evdev, boolean down) {
        int bit = evdev == 272 ? 1 : evdev == 273 ? 2 : evdev == 274 ? 4 : 0;
        if (bit == 0) return;
        if (((mouseButtonMask & bit) != 0) == down) return;
        if (down) mouseButtonMask |= bit;
        else mouseButtonMask &= ~bit;
        sendButton(evdev, down);
    }
    private void releaseHardwareMouseButtons() {
        for (int evdev : new int[] {272, 273, 274})
            sendHardwareMouseButton(evdev, false);
    }
    private void cancelGesture() {
        gestures.removeCallbacks(longPress);
        releaseHardwareMouseButtons();
        if (pressed) sendButton(272, false);
        if (inputMode == MODE_DIRECT_TOUCH)
            WaylandCompositor.nativeSendTouch(3, -1, 0, 0);
        pressed = dragStarted = touchMoved = multipleTouch = scrolled = overlayGesture = false;
        twoFingerScrollStarted = false;
        scrollResidualY = 0f;
        scrollState.reset();
        primaryPointer = -1;
    }
    public void cancelPointerGesture() { cancelGesture(); }

    public void setGuestCursor(Bitmap bitmap, int hotspotX, int hotspotY, boolean hidden) {
        guestCursorHidden = hidden;
        if (bitmap != null && !hidden) {
            guestIcon = PointerIcon.create(bitmap,
                    Math.max(0, Math.min(bitmap.getWidth() - 1, hotspotX)),
                    Math.max(0, Math.min(bitmap.getHeight() - 1, hotspotY)));
        } else {
            guestIcon = null;
        }
        refreshPointerOwnership();
        invalidate();
    }

    /** Make the same Android View own either hardware cursor or overlay. */
    private void refreshPointerOwnership() {
        if (PointerStartupPolicy.hideSystemCursor(inputMode, captured,
                guestCursorHidden, touchpadCursorActive && guestIcon != null))
            setPointerIcon(PointerIcon.getSystemIcon(getContext(), PointerIcon.TYPE_NULL));
        else
            setPointerIcon(guestIcon); // null restores the system default
    }

    @Override public PointerIcon onResolvePointerIcon(MotionEvent event, int pointerIndex) {
        if (PointerStartupPolicy.hideSystemCursor(
                inputMode, captured, guestCursorHidden,
                touchpadCursorActive && guestIcon != null))
            return PointerIcon.getSystemIcon(getContext(), PointerIcon.TYPE_NULL);
        return guestIcon != null ? guestIcon : super.onResolvePointerIcon(event, pointerIndex);
    }

    public void setCapture(boolean on) {
        if (on) {
            requestFocus();
            requestPointerCapture();
        } else {
            releaseAllKeys();
            releasePointerCapture();
        }
    }

    @Override public void onPointerCaptureChange(boolean hasCapture) {
        super.onPointerCaptureChange(hasCapture);
        captured = hasCapture;
        if (hasCapture && !pointerInitialized)
            absolute(getWidth() / 2f, getHeight() / 2f);
        if (!hasCapture) releaseAllKeys();
        refreshPointerOwnership();
        if (onCursorMove != null) onCursorMove.run();
    }

    public void releaseAllKeys() {
        cancelGesture();
        for (Integer code : new HashSet<>(keys)) WaylandCompositor.nativeSendKey(code, 0);
        keys.clear();
        syntheticShiftKeyUps.clear();
        for (int btn : new int[] {272, 273, 274}) WaylandCompositor.nativeSendSceneInput(3, btn, 0);
        mouseButtonMask = 0;
    }

    @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
        return deliverKey(event, true);
    }

    @Override public boolean onKeyUp(int keyCode, KeyEvent event) {
        return deliverKey(event, false);
    }

    private boolean deliverKey(KeyEvent e, boolean down) {
        if (e.getKeyCode() == KeyEvent.KEYCODE_BACK) return false;
        int code = e.getScanCode();
        if (code <= 0) code = fallbackEvdev(e.getKeyCode());
        if (code <= 0 || code > 767) return false;
        if (!down && syntheticShiftKeyUps.remove(code)) return true;
        if (down) {
            syntheticShiftKeyUps.remove(code);
            boolean alpha = e.getKeyCode() >= KeyEvent.KEYCODE_A &&
                    e.getKeyCode() <= KeyEvent.KEYCODE_Z;
            int unicode = e.getUnicodeChar();
            boolean uppercase = (unicode >= 'A' && unicode <= 'Z') ||
                    (unicode == 0 && (e.isShiftPressed() ^ e.isCapsLockOn()));
            boolean shortcut = e.isCtrlPressed() || e.isAltPressed() || e.isMetaPressed();
            if (ImeVirtualShiftPolicy.needsScopedShift(e.getScanCode() <= 0,
                    alpha, uppercase, keys.contains(42) || keys.contains(54),
                    shortcut) && !keys.contains(code)) {
                // Virtual IMEs often encode Shift in metaState only. A
                // complete down/up pair avoids leaving Shift held when the
                // IME omits or delays the matching key-up callback.
                WaylandCompositor.nativeSendKey(42, 1);
                WaylandCompositor.nativeSendKey(code, 1);
                WaylandCompositor.nativeSendKey(code, 0);
                WaylandCompositor.nativeSendKey(42, 0);
                syntheticShiftKeyUps.add(code);
                noteActualScanlessAscii(e);
                return true;
            }
        }
        if (down) {
            if (!keys.add(code)) return true;
        } else keys.remove(code);
        WaylandCompositor.nativeSendKey(code, down ? 1 : 0);
        if (down) noteActualScanlessAscii(e);
        return true;
    }

    private void noteActualScanlessAscii(KeyEvent e) {
        // Called ONLY after a down was actually sent to the native seat;
        // repeat/ignored downs and modifier-only keys never update history.
        if (e.getScanCode() > 0 || e.isCtrlPressed() ||
                e.isAltPressed() || e.isMetaPressed()) return;
        int ch = e.getUnicodeChar();
        // Never guess English from KEYCODE_A..Z when getUnicodeChar()==0:
        // Samsung can reuse those virtual codes on a Korean keyboard and
        // such a guess would incorrectly disarm Korean-deletion protection.
        if (ch >= 0x20 && ch <= 0x7e)
            ImeBridge.noteDirectAsciiKey(ch);
    }

    private void recordImeEvent(int characters) {
        imeEventCount++;
        imeCharacterCount += Math.max(0, characters);
        if (imeEventCount % 64 == 0) {
            android.util.Log.i("AlrootWayland", "IME Android event-count=" +
                    imeEventCount + " character-count=" + imeCharacterCount +
                    " X11-active=" + ImeBridge.rootfulX11Active());
        }
    }

    private void traceFirstEditor(String operation, CharSequence value, boolean accepted) {
        // Persist the event decision without plaintext, before Android's
        // volatile logcat ring drops the original first-field failure.
        ImeFlowTrace.record("ANDROID_" + operation, value, accepted);
        // Bounded, metadata-only report for the user's *first* editor entry.
        // There is no plaintext, cursor-surrounding text, clipboard, or key
        // content in logcat; only route/length/script classes and decisions.
        if (++imeFirstEditorDiagnostics > 128) return;
        boolean latin = false, korean = false, other = false;
        int length = value == null ? 0 : value.length();
        for (int i = 0; i < length; ) {
            int cp = Character.codePointAt(value, i);
            if (cp >= 0x20 && cp < 0x80) latin = true;
            else if ((cp >= 0x1100 && cp <= 0x11ff) ||
                    (cp >= 0x3130 && cp <= 0x318f) ||
                    (cp >= 0xac00 && cp <= 0xd7af)) korean = true;
            else other = true;
            i += Character.charCount(cp);
        }
        String script = length == 0 ? "EMPTY" :
                korean && latin ? "MIXED" : korean ? "KOREAN" :
                latin && !other ? "ASCII" : "OTHER";
        android.util.Log.i("AlrootWayland",
                "IME P18 first-editor-event=" + imeFirstEditorDiagnostics +
                " op=" + operation + " length=" + length +
                " script=" + script + " delivered=" + accepted);
    }

    private int fallbackEvdev(int key) {
        // Some Android software keyboards send KeyEvent rather than
        // InputConnection.commitText (with scanCode=0). Map their logical
        // AOSP keycodes to Linux evdev keycodes on our isolated X11 seat.
        if (key >= KeyEvent.KEYCODE_A && key <= KeyEvent.KEYCODE_Z) {
            final int[] qwerty = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                    49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
            return qwerty[key - KeyEvent.KEYCODE_A];
        }
        if (key >= KeyEvent.KEYCODE_0 && key <= KeyEvent.KEYCODE_9) {
            final int[] digits = {11, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            return digits[key - KeyEvent.KEYCODE_0];
        }
        switch (key) {
            case KeyEvent.KEYCODE_ENTER: return 28;
            case KeyEvent.KEYCODE_SHIFT_LEFT: return 42;
            case KeyEvent.KEYCODE_SHIFT_RIGHT: return 54;
            case KeyEvent.KEYCODE_DEL: return 14;
            case KeyEvent.KEYCODE_FORWARD_DEL: return 111;
            case KeyEvent.KEYCODE_SPACE: return 57;
            case KeyEvent.KEYCODE_TAB: return 15;
            case KeyEvent.KEYCODE_ESCAPE: return 1;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 105;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 106;
            case KeyEvent.KEYCODE_DPAD_UP: return 103;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 108;
            case KeyEvent.KEYCODE_COMMA: return 51;
            case KeyEvent.KEYCODE_PERIOD: return 52;
            case KeyEvent.KEYCODE_MINUS: return 12;
            case KeyEvent.KEYCODE_EQUALS: return 13;
            case KeyEvent.KEYCODE_APOSTROPHE: return 40;
            case KeyEvent.KEYCODE_SEMICOLON: return 39;
            case KeyEvent.KEYCODE_SLASH: return 53;
            case KeyEvent.KEYCODE_LEFT_BRACKET: return 26;
            case KeyEvent.KEYCODE_RIGHT_BRACKET: return 27;
            case KeyEvent.KEYCODE_BACKSLASH: return 43;
            default: return 0;
        }
    }

    public void updateTransform(int guestWidth, int guestHeight, int mode) {
        float w = Math.max(1, getWidth()), h = Math.max(1, getHeight());
        if (mode == 2) {
            picture.set(0, 0, w, h);
        } else {
            float factor = mode == 3 ? Math.max(w / guestWidth, h / guestHeight)
                    : Math.min(w / guestWidth, h / guestHeight);
            float pw = guestWidth * factor, ph = guestHeight * factor;
            picture.set((w - pw) / 2, (h - ph) / 2, (w + pw) / 2, (h + ph) / 2);
        }
        pointerScaleX = guestWidth / Math.max(1, picture.width());
        pointerScaleY = guestHeight / Math.max(1, picture.height());
        if (pointerInitialized) {
            lastX = Math.max(picture.left, Math.min(picture.right - 1, lastX));
            lastY = Math.max(picture.top, Math.min(picture.bottom - 1, lastY));
        }
    }

    private void absolute(float x, float y) {
        android.content.SharedPreferences prefs = getContext().getSharedPreferences("display", Context.MODE_PRIVATE);
        int w = prefs.getInt("width", 1920), h = prefs.getInt("height", 1080);
        updateTransform(w, h, prefs.getBoolean("fullscreen", true)
                ? 1 : prefs.getInt("mode", 1));
        // P19 rootful Xwayland: the visual cursor and X11 hit test must use
        // ONE authoritative transform.  The old event type 2 normalised
        // Android view coordinates to 1920x1080 and then inverted a *cached*
        // Vulkan scene/output map.  That map can lag behind PiP/fullscreen
        // transitions or an Xwayland resize and makes clicking drift away
        // from the rendered cursor while the desktop keeps running.
        // Type 12 is explicitly guest-scene coordinates, not output-space:
        // do the same inverse picture FIT map that already positions the
        // Android cursor/overlay and let the compositor clamp to its live
        // Xwayland scene without applying a second transform.
        int gx = Math.max(0, Math.min(w - 1,
                Math.round((x - picture.left) * pointerScaleX)));
        int gy = Math.max(0, Math.min(h - 1,
                Math.round((y - picture.top) * pointerScaleY)));
        WaylandCompositor.nativeSendSceneInput(12, gx, gy);
        lastX = Math.max(picture.left, Math.min(picture.right - 1, x));
        lastY = Math.max(picture.top, Math.min(picture.bottom - 1, y));
        pointerInitialized = true;
        if (onCursorMove != null) onCursorMove.run();
    }

    private void relative(float dx, float dy) {
        if (!pointerInitialized) {
            lastX = getWidth() / 2f;
            lastY = getHeight() / 2f;
            pointerInitialized = true;
        }
        lastX = Math.max(picture.left, Math.min(picture.right - 1, lastX + dx));
        lastY = Math.max(picture.top, Math.min(picture.bottom - 1, lastY + dy));
        if (inputMode == MODE_TOUCHPAD && !captured) {
            // P19: Android's touchpad overlay and the Wayland pointer used to
            // maintain INDEPENDENT relative position accumulators. Rounding,
            // scene resizing, X11 warp/focus and clamping could gradually
            // diverge, making taps hit something other than the visible cursor.
            // The software pointer position is now authoritative in ordinary
            // desktop/touchpad mode: send an absolute output-space position
            // on each gesture delta. Pointer-captured games still receive
            // true relative deltas below and are not changed.
            absolute(lastX, lastY);
        } else {
            WaylandCompositor.nativeSendSceneInput(6,
                    Math.round(dx * pointerScaleX * 256f),
                    Math.round(dy * pointerScaleY * 256f));
        }
        if (onCursorMove != null) onCursorMove.run();
    }
    private void acceleratedRelative(float dx, float dy) {
        float distance = (float)Math.hypot(dx, dy);
        float gain = (pointerSpeed / 100f) *
                (1f + acceleration / 100f * Math.min(1f, distance / (35f * getResources().getDisplayMetrics().density)));
        relative(dx * gain, dy * gain);
    }

    private int normalizeTouchX(float x) {
        return Math.max(0, Math.min(1920, Math.round(x / Math.max(1, getWidth()) * 1920f)));
    }
    private int normalizeTouchY(float y) {
        return Math.max(0, Math.min(1080, Math.round(y / Math.max(1, getHeight()) * 1080f)));
    }
    private void sendNativeTouch(int action, MotionEvent event, int index) {
        if (index < 0 || index >= event.getPointerCount()) return;
        int id = event.getPointerId(index);
        if (id >= 0 && id < 16)
            WaylandCompositor.nativeSendTouch(action, id,
                    normalizeTouchX(event.getX(index)), normalizeTouchY(event.getY(index)));
    }

    private static int evdevButton(int button) {
        if (button == MotionEvent.BUTTON_PRIMARY) return 272;
        if (button == MotionEvent.BUTTON_SECONDARY) return 273;
        if (button == MotionEvent.BUTTON_TERTIARY) return 274;
        return 0;
    }

    @Override public boolean onCapturedPointerEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_HOVER_MOVE) {
            acceleratedRelative(event.getX(), event.getY());
            return true;
        }
        if (action == MotionEvent.ACTION_SCROLL) {
            WaylandCompositor.nativeSendSceneInput(4,
                    Math.round(event.getAxisValue(MotionEvent.AXIS_VSCROLL)), 0);
            return true;
        }
        if (action == MotionEvent.ACTION_BUTTON_PRESS || action == MotionEvent.ACTION_BUTTON_RELEASE) {
            // Captured pointer events provide relative deltas only, never
            // treat their x/y as physical screen coordinates.
            sendHardwareMouseButton(evdevButton(event.getActionButton()),
                    action == MotionEvent.ACTION_BUTTON_PRESS);
            return true;
        }
        return true;
    }

    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (event.isFromSource(android.view.InputDevice.SOURCE_MOUSE)) {
            // Actual mouse input returns ownership to the Android hardware
            // pointer; do not draw the same guest bitmap in CursorOverlay.
            if (touchpadCursorActive) {
                touchpadCursorActive = false;
                refreshPointerOwnership();
                if (onCursorMove != null) onCursorMove.run();
            }
            if (event.getActionMasked() == MotionEvent.ACTION_HOVER_MOVE ||
                    event.getActionMasked() == MotionEvent.ACTION_MOVE) {
                absolute(event.getX(), event.getY());
                return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
                WaylandCompositor.nativeSendSceneInput(4,
                        Math.round(event.getAxisValue(MotionEvent.AXIS_VSCROLL)), 0);
                return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS ||
                    event.getActionMasked() == MotionEvent.ACTION_BUTTON_RELEASE) {
                // Hardware mouse buttons may arrive without HOVER_MOVE.
                // Sync the guest coordinate at the *button's* location before
                // sending Wayland wl_pointer.button, so cursor and hit-test
                // cannot refer to a stale previous guest coordinate.
                absolute(event.getX(), event.getY());
                sendHardwareMouseButton(evdevButton(event.getActionButton()),
                        event.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS);
                return true;
            }
        }
        return super.onGenericMotionEvent(event);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int a = event.getActionMasked();
        int index = event.getActionIndex();
        if (event.isFromSource(android.view.InputDevice.SOURCE_MOUSE)) {
            // Android's physical mouse ACTION_DOWN/UP is not a finger gesture.
            // Feeding it through MODE_TOUCHPAD would hide the real pointer,
            // then tap an unrelated cached guest cursor position. Generic
            // ACTION_BUTTON_PRESS may also arrive; deduplicate both routes.
            if (touchpadCursorActive) {
                touchpadCursorActive = false;
                refreshPointerOwnership();
                if (onCursorMove != null) onCursorMove.run();
            }
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_UP ||
                    a == MotionEvent.ACTION_MOVE) {
                absolute(event.getX(), event.getY());
                if (a == MotionEvent.ACTION_DOWN &&
                        (event.getButtonState() & MotionEvent.BUTTON_PRIMARY) != 0)
                    sendHardwareMouseButton(272, true);
                else if (a == MotionEvent.ACTION_UP)
                    sendHardwareMouseButton(272, false);
            } else if (a == MotionEvent.ACTION_CANCEL) {
                releaseHardwareMouseButtons();
            }
            return true;
        }
        if (a == MotionEvent.ACTION_DOWN)
            ImeBridge.newEditorContext();
        if (!overlayGesture && event.getPointerCount() >= 3 &&
                a == MotionEvent.ACTION_POINTER_DOWN) {
            cancelGesture();
            overlayGesture = true;
            if (overlayToggle != null) overlayToggle.run();
            return true;
        }
        if (overlayGesture) {
            if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) overlayGesture = false;
            return true;
        }
        if (inputMode == MODE_DIRECT_TOUCH) {
            switch (a) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: sendNativeTouch(0, event, index); break;
                case MotionEvent.ACTION_MOVE:
                    for (int i = 0; i < event.getPointerCount(); i++)
                        sendNativeTouch(1, event, i);
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP: sendNativeTouch(2, event, index); break;
                case MotionEvent.ACTION_CANCEL:
                    WaylandCompositor.nativeSendTouch(3, -1, 0, 0); break;
            }
            return true;
        }
        if (inputMode == MODE_VIRTUAL_TOUCHSCREEN) {
            if (a == MotionEvent.ACTION_DOWN) {
                absolute(event.getX(), event.getY());
                sendButton(272, true);
                pressed = true;
            } else if (a == MotionEvent.ACTION_MOVE && pressed) {
                absolute(event.getX(), event.getY());
            } else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
                if (pressed) sendButton(272, false);
                pressed = false;
            }
            return true;
        }
        // Relative touchpad mode: finger motion moves a persistent cursor,
        // tap clicks, long hold drags, and two fingers scroll.
        if (a == MotionEvent.ACTION_DOWN) {
            if (!pointerInitialized) absolute(getWidth() / 2f, getHeight() / 2f);
            touchpadCursorActive = true;
            refreshPointerOwnership();
            if (onCursorMove != null) onCursorMove.run();
            primaryPointer = event.getPointerId(0);
            touchStarted = event.getEventTime();
            prevX = touchStartX = event.getX(0);
            prevY = touchStartY = event.getY(0);
            touchMoved = multipleTouch = scrolled = dragStarted = false;
            twoFingerScrollStarted = false;
            scrollResidualY = 0f;
            scrollState.reset();
            gestures.postDelayed(longPress, 420);
        } else if (a == MotionEvent.ACTION_POINTER_DOWN) {
            gestures.removeCallbacks(longPress);
            multipleTouch = true;
            // A gesture involving multiple fingers is exclusively scroll or
            // right-click. Do not resume single-finger relative movement
            // until the NEXT fresh ACTION_DOWN after all fingers lift.
            // Otherwise the surviving finger is compared to the original
            // pointer's touchStartX/Y and causes large diagonal jumps.
            primaryPointer = -1;
            if (pressed) { sendButton(272, false); pressed = false; }
            if (event.getPointerCount() == 2) {
                prevCentroidY = (event.getY(0) + event.getY(1)) * .5f;
                twoFingerScrollStarted = false;
                scrollResidualY = 0f;
                scrollState.reset();
            }
        } else if (a == MotionEvent.ACTION_POINTER_UP) {
            // Never reassign primaryPointer to the remaining finger.
            primaryPointer = -1;
            multipleTouch = true;
        } else if (a == MotionEvent.ACTION_MOVE) {
            if (event.getPointerCount() >= 2) {
                float cy = (event.getY(0) + event.getY(1)) * .5f;
                float delta = cy - prevCentroidY;
                int units = scrollState.move(delta);
                if (scrollState.isScrolling()) scrolled = true;
                if (units != 0)
                    WaylandCompositor.nativeSendSceneInput(11, units, 0);
                prevCentroidY = cy;
            } else if (primaryPointer >= 0) {
                int slot = event.findPointerIndex(primaryPointer);
                if (slot >= 0) {
                    float x = event.getX(slot), y = event.getY(slot);
                    float distance = (float)Math.hypot(x - touchStartX, y - touchStartY);
                    if (!touchMoved) {
                        // Suppress sub-touchSlop jitter, especially while the
                        // finger taps without any intentional pointer motion.
                        if (distance > touchSlop) {
                            touchMoved = true;
                            gestures.removeCallbacks(longPress);
                            float pass = (distance - touchSlop) / distance;
                            acceleratedRelative((x - touchStartX) * pass,
                                    (y - touchStartY) * pass);
                        }
                    } else {
                        acceleratedRelative(x - prevX, y - prevY);
                    }
                    prevX = x; prevY = y;
                }
            }
        } else if (a == MotionEvent.ACTION_UP) {
            gestures.removeCallbacks(longPress);
            if (pressed) sendButton(272, false);
            else if (multipleTouch && !scrolled &&
                    event.getEventTime() - touchStarted < 450) {
                sendButton(273, true);
                sendButton(273, false);
            }
            else if (!multipleTouch && !touchMoved &&
                    event.getEventTime() - touchStarted <= 500) {
                sendButton(272, true);
                sendButton(272, false);
            }
            pressed = false;
            primaryPointer = -1;
        } else if (a == MotionEvent.ACTION_CANCEL) {
            cancelGesture();
        }
        return true;
    }

    @Override public boolean onCheckIsTextEditor() { return true; }

    @Override public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT |
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS |
                InputType.TYPE_TEXT_VARIATION_NORMAL;
        // IME_ACTION_NONE can remove the enter/newline key on some on-device
        // keyboards. Match the proven Termux-X11 input editor flags.
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN;
        outAttrs.actionLabel = "↵";
        // X11 owns the real editor and there is no Android Editable buffer.
        // Do not advertise a concrete (1,1) cursor in a one-space phantom
        // editor, which Samsung may interpret as a real composing range.
        // Match Termux-X11's unspecified initial selection instead.
        outAttrs.initialSelStart = -1;
        outAttrs.initialSelEnd = -1;
        android.util.Log.i("AlrootWayland",
                "IME InputConnection " +
                (imeConnections.hasConnection() ? "reused" : "created") +
                " focused=" + hasFocus() + " window=" + hasWindowFocus());
        ImeFlowTrace.record(imeConnections.hasConnection()
                ? "ANDROID_REUSE_INPUT_CONNECTION"
                : "ANDROID_CREATE_INPUT_CONNECTION", null, true);
        // X11 apps own the actual text. Android must never pretend this
        // SurfaceView has a real editable buffer; Samsung/Gboard otherwise
        // rely on stale cursor/selection state and send empty commitText.
        return imeConnections.getOrCreate(() -> new BaseInputConnection(this, false) {
            private boolean consumedDraftBackspace;
            private final ImeCursorTracker synthetic = new ImeCursorTracker();
            private int lastLoggedPhantomDeletes;
            private int avoidedSelectionEchoes;
            private void reportCursor() {
                InputMethodManager manager = (InputMethodManager)
                        getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
                if (manager == null) return;
                ImeCursorTracker.Report state = synthetic.report();
                // Never acknowledge a stale/filtered commit or a no-op
                // begin/endBatchEdit by re-publishing the SAME position.
                // Samsung Keyboard can interpret that redundant callback as
                // a request to resend its old English composing buffer.
                if (!state.notify) {
                    if (++avoidedSelectionEchoes % 128 == 0)
                        android.util.Log.i("AlrootWayland",
                                "IME P17 redundant selection notifications avoided=" +
                                avoidedSelectionEchoes + " (no text captured)");
                    return;
                }
                int position = state.cursor;
                boolean reset = state.reset;
                Runnable report = () -> {
                    if (reset)
                        manager.updateSelection(InputSurfaceView.this,
                                -1, -1, -1, -1);
                    manager.updateSelection(InputSurfaceView.this,
                            position, position, -1, -1);
                };
                if (Looper.myLooper() == Looper.getMainLooper()) report.run();
                else post(report);
            }
            @Override public boolean beginBatchEdit() {
                synthetic.begin();
                return true;
            }
            @Override public boolean endBatchEdit() {
                boolean finished = synthetic.end();
                if (finished) reportCursor();
                return !finished;
            }
            @Override public android.text.Editable getEditable() { return null; }
            @Override public CharSequence getTextBeforeCursor(int n, int flags) { return " "; }
            @Override public CharSequence getTextAfterCursor(int n, int flags) { return " "; }
            @Override public boolean setComposingRegion(int start, int end) { return true; }
            @Override public SurroundingText getSurroundingText(int before, int after, int flags) {
                return new SurroundingText(before == 0 || after == 0 ? " " : "  ",
                        1, 1, -1);
            }
            @Override public boolean setSelection(int start, int end) {
                synthetic.selected(start, end);
                if (!synthetic.inBatch()) reportCursor();
                return true;
            }
            @Override public boolean commitText(CharSequence text, int newCursorPosition) {
                boolean x11 = ImeBridge.rootfulX11Active();
                recordImeEvent(text.length());
                boolean inserted;
                if (x11) inserted = ImeBridge.commitComposed(text.toString());
                else {
                    WaylandCompositor.textInputCommit(text.toString());
                    inserted = true;
                }
                if (inserted)
                    synthetic.committed(text.length(), newCursorPosition);
                traceFirstEditor("COMMIT", text, inserted);
                if (!synthetic.inBatch()) reportCursor();
                return true;
            }
            @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
                boolean x11 = ImeBridge.rootfulX11Active();
                recordImeEvent(text.length());
                if (x11) {
                    ImeBridge.composing(text.toString());
                } else {
                    WaylandCompositor.textInputPreedit(text.toString(), text.length());
                }
                traceFirstEditor("COMPOSE", text, true);
                return true;
            }
            @Override public boolean finishComposingText() {
                ImeBridge.finishComposing();
                traceFirstEditor("FINISH", null, true);
                return true;
            }
            @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                if (synthetic.phantomDelete(beforeLength, afterLength)) {
                    if (ImeBridge.rootfulX11Active() && beforeLength > 0)
                        ImeBridge.phantomSurroundingDelete();
                    int count = synthetic.ignoredPhantomDeletes();
                    if (count != lastLoggedPhantomDeletes && count % 8 == 0) {
                        lastLoggedPhantomDeletes = count;
                        android.util.Log.i("AlrootWayland",
                                "IME ignored phantom editor-space cleanup count=" + count);
                    }
                    traceFirstEditor("DELETE_PHANTOM", null, false);
                    return true;
                }
                if (ImeBridge.rootfulX11Active()) {
                    boolean hadDraft = ImeBridge.hasPendingPreedit();
                    boolean processed = ImeBridge.delete(beforeLength, afterLength);
                    if (!hadDraft && processed) {
                        synthetic.deleted(beforeLength);
                        if (!synthetic.inBatch()) reportCursor();
                    }
                    traceFirstEditor(hadDraft ? "DELETE_PREEDIT" : "DELETE_REMOTE",
                            null, processed);
                }
                else {
                    WaylandCompositor.textInputDelete(beforeLength, afterLength);
                    traceFirstEditor("DELETE_WAYLAND", null, true);
                }
                return true;
            }
            @Override public boolean deleteSurroundingTextInCodePoints(int before, int after) {
                return deleteSurroundingText(before, after);
            }
            @Override public boolean performEditorAction(int action) {
                deliverKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER), true);
                deliverKey(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER), false);
                return true;
            }
            @Override public boolean sendKeyEvent(KeyEvent event) {
                if (event.getKeyCode() == KeyEvent.KEYCODE_DEL) {
                    if (event.getAction() == KeyEvent.ACTION_DOWN &&
                            ImeBridge.rootfulX11Active()) {
                        ImeFlowTrace.record("ANDROID_KEY_DEL_DOWN", null, true);
                        boolean draftBeforeDelete = ImeBridge.hasPendingPreedit();
                        consumedDraftBackspace = ImeBridge.deletePendingPreedit();
                        if (consumedDraftBackspace) {
                            if (!draftBeforeDelete) {
                                synthetic.deleted(1);
                                if (!synthetic.inBatch()) reportCursor();
                            }
                            return true;
                        }
                    }
                    if (event.getAction() == KeyEvent.ACTION_UP && consumedDraftBackspace) {
                        consumedDraftBackspace = false;
                        return true;
                    }
                }
                if (event.getAction() == KeyEvent.ACTION_MULTIPLE) {
                    String chars = event.getCharacters();
                    if (chars != null && !chars.isEmpty()) {
                        if (ImeBridge.rootfulX11Active()) ImeBridge.commitComposed(chars);
                        else WaylandCompositor.textInputCommit(chars);
                        return true;
                    }
                }
                if (event.getAction() == KeyEvent.ACTION_DOWN ||
                        event.getAction() == KeyEvent.ACTION_UP) {
                    if (deliverKey(event, event.getAction() == KeyEvent.ACTION_DOWN)) {
                        return true;
                    }
                }
                return super.sendKeyEvent(event);
            }
        });
    }
}
