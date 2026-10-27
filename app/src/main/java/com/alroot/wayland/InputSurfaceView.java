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
import android.view.inputmethod.SurroundingText;
import com.droiddeck.launcher.wayland.WaylandCompositor;
import java.util.HashSet;
import java.util.Set;

public final class InputSurfaceView extends SurfaceView {
    public static final int MODE_TOUCHPAD = 0;
    public static final int MODE_VIRTUAL_TOUCHSCREEN = 1;
    public static final int MODE_DIRECT_TOUCH = 2;
    private final Set<Integer> keys = new HashSet<>();
    private final Handler gestures = new Handler(Looper.getMainLooper());
    private final float touchSlop;
    private int inputMode;
    private int pointerSpeed;
    private int acceleration;
    private int primaryPointer = -1;
    private boolean touchpadCursorActive;
    private long touchStarted;
    private float touchStartX, touchStartY, prevX, prevY, prevCentroidY;
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
    private float lastX, lastY;
    private boolean pointerInitialized;
    private float pointerScaleX = 1, pointerScaleY = 1;
    private int imeEventCount;
    private int imeCharacterCount;
    private final RectF picture = new RectF();
    private PointerIcon guestIcon;
    private boolean guestCursorHidden;
    private Runnable onCursorMove;

    public InputSurfaceView(Context context) {
        super(context);
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        SharedPreferences p = context.getSharedPreferences("display", Context.MODE_PRIVATE);
        inputMode = p.getInt("input_mode", MODE_TOUCHPAD);
        pointerSpeed = p.getInt("pointer_speed", 190);
        acceleration = p.getInt("pointer_accel", 110);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setClickable(true);
        setOnClickListener(v -> requestFocus());
    }

    public boolean isCaptured() { return captured; }
    public float pointerX() { return lastX; }
    public float pointerY() { return lastY; }
    public boolean isSoftwareCursorWanted() {
        return captured || (inputMode == MODE_TOUCHPAD && touchpadCursorActive);
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
    }
    private void sendButton(int evdev, boolean down) {
        WaylandCompositor.nativeSendSceneInput(3, evdev, down ? 1 : 0);
    }
    private void cancelGesture() {
        gestures.removeCallbacks(longPress);
        if (pressed) sendButton(272, false);
        if (inputMode == MODE_DIRECT_TOUCH)
            WaylandCompositor.nativeSendTouch(3, -1, 0, 0);
        pressed = dragStarted = touchMoved = multipleTouch = scrolled = overlayGesture = false;
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
        invalidate();
    }

    @Override public PointerIcon onResolvePointerIcon(MotionEvent event, int pointerIndex) {
        if (captured || guestCursorHidden)
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
        if (onCursorMove != null) onCursorMove.run();
    }

    public void releaseAllKeys() {
        cancelGesture();
        for (Integer code : new HashSet<>(keys)) WaylandCompositor.nativeSendKey(code, 0);
        keys.clear();
        for (int btn : new int[] {272, 273, 274}) WaylandCompositor.nativeSendSceneInput(3, btn, 0);
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
        if (down) {
            if (!keys.add(code)) return true;
        } else keys.remove(code);
        WaylandCompositor.nativeSendKey(code, down ? 1 : 0);
        return true;
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
        // Compositor JNI expects 1920x1080 *physical output* coordinates and
        // itself inverts the aspect-fit map to logical X11 scene coordinates.
        // Feeding a logical 1280/1920 guest pixel here would double-transform.
        int px = Math.min(1920, Math.max(0, Math.round(x / Math.max(1, getWidth()) * 1920f)));
        int py = Math.min(1080, Math.max(0, Math.round(y / Math.max(1, getHeight()) * 1080f)));
        WaylandCompositor.nativeSendSceneInput(2, px, py);
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
        WaylandCompositor.nativeSendSceneInput(6,
                Math.round(dx * pointerScaleX * 256f),
                Math.round(dy * pointerScaleY * 256f));
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
            int button = evdevButton(event.getActionButton());
            if (button != 0) WaylandCompositor.nativeSendSceneInput(
                    3, button, action == MotionEvent.ACTION_BUTTON_PRESS ? 1 : 0);
            return true;
        }
        return true;
    }

    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (event.isFromSource(android.view.InputDevice.SOURCE_MOUSE)) {
            if (touchpadCursorActive) {
                touchpadCursorActive = false;
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
                int btn = evdevButton(event.getActionButton());
                if (btn != 0) WaylandCompositor.nativeSendSceneInput(3, btn,
                        event.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS ? 1 : 0);
                return true;
            }
        }
        return super.onGenericMotionEvent(event);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int a = event.getActionMasked();
        int index = event.getActionIndex();
        if (a == MotionEvent.ACTION_DOWN)
            ImeBridge.resetComposing();
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
            if (onCursorMove != null) onCursorMove.run();
            primaryPointer = event.getPointerId(0);
            touchStarted = event.getEventTime();
            prevX = touchStartX = event.getX(0);
            prevY = touchStartY = event.getY(0);
            touchMoved = multipleTouch = scrolled = dragStarted = false;
            gestures.postDelayed(longPress, 420);
        } else if (a == MotionEvent.ACTION_POINTER_DOWN) {
            gestures.removeCallbacks(longPress);
            multipleTouch = true;
            if (pressed) { sendButton(272, false); pressed = false; }
            if (event.getPointerCount() == 2)
                prevCentroidY = (event.getY(0) + event.getY(1)) * .5f;
        } else if (a == MotionEvent.ACTION_POINTER_UP) {
            if (event.getPointerId(index) == primaryPointer && event.getPointerCount() > 1) {
                int other = index == 0 ? 1 : 0;
                primaryPointer = event.getPointerId(other);
                prevX = event.getX(other);
                prevY = event.getY(other);
            }
            multipleTouch = true;
        } else if (a == MotionEvent.ACTION_MOVE) {
            if (event.getPointerCount() >= 2) {
                float cy = (event.getY(0) + event.getY(1)) * .5f;
                float delta = cy - prevCentroidY;
                int units = Math.round(delta / (32f * getResources().getDisplayMetrics().density) * 120f);
                if (units != 0) {
                    scrolled = true;
                    WaylandCompositor.nativeSendSceneInput(11, units, 0);
                }
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
        outAttrs.initialSelStart = 1;
        outAttrs.initialSelEnd = 1;
        android.util.Log.i("AlrootWayland",
                "IME InputConnection created focused=" + hasFocus() + " window=" + hasWindowFocus());
        // X11 apps own the actual text. Android must never pretend this
        // SurfaceView has a real editable buffer; Samsung/Gboard otherwise
        // rely on stale cursor/selection state and send empty commitText.
        return new BaseInputConnection(this, false) {
            @Override public android.text.Editable getEditable() { return null; }
            @Override public CharSequence getTextBeforeCursor(int n, int flags) { return " "; }
            @Override public CharSequence getTextAfterCursor(int n, int flags) { return " "; }
            @Override public boolean setComposingRegion(int start, int end) { return true; }
            @Override public SurroundingText getSurroundingText(int before, int after, int flags) {
                return new SurroundingText(before == 0 || after == 0 ? " " : "  ",
                        1, 1, -1);
            }
            @Override public boolean setSelection(int start, int end) { return true; }
            @Override public boolean commitText(CharSequence text, int newCursorPosition) {
                boolean x11 = ImeBridge.rootfulX11Active();
                recordImeEvent(text.length());
                if (x11) ImeBridge.commitComposed(text.toString());
                else WaylandCompositor.textInputCommit(text.toString());
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
                return true;
            }
            @Override public boolean finishComposingText() {
                ImeBridge.finishComposing();
                return true;
            }
            @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                if (ImeBridge.rootfulX11Active()) ImeBridge.delete(beforeLength, afterLength);
                else WaylandCompositor.textInputDelete(beforeLength, afterLength);
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
        };
    }
}
