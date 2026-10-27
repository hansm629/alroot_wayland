package com.alroot.wayland;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.RectF;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.SurfaceView;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import com.droiddeck.launcher.wayland.WaylandCompositor;
import java.util.HashSet;
import java.util.Set;

public final class InputSurfaceView extends SurfaceView {
    private final Set<Integer> keys = new HashSet<>();
    private boolean captured;
    private boolean primaryTouchDown;
    private float lastX, lastY;
    private boolean pointerInitialized;
    private float pointerScaleX = 1, pointerScaleY = 1;
    private final RectF picture = new RectF();
    private PointerIcon guestIcon;
    private boolean guestCursorHidden;
    private Runnable onCursorMove;

    public InputSurfaceView(Context context) {
        super(context);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setClickable(true);
        setOnClickListener(v -> requestFocus());
    }

    public boolean isCaptured() { return captured; }
    public float pointerX() { return lastX; }
    public float pointerY() { return lastY; }
    public float cursorScaleX() { return 1f / pointerScaleX; }
    public float cursorScaleY() { return 1f / pointerScaleY; }
    public void setCursorMoveListener(Runnable listener) { onCursorMove = listener; }

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
        for (Integer code : new HashSet<>(keys)) WaylandCompositor.nativeSendKey(code, 0);
        keys.clear();
        for (int btn : new int[] {272, 273, 274}) WaylandCompositor.nativeSendSceneInput(3, btn, 0);
        primaryTouchDown = false;
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

    private int fallbackEvdev(int key) {
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
        int w = prefs.getInt("width", 1280), h = prefs.getInt("height", 720);
        updateTransform(w, h, prefs.getInt("mode", 1));
        int px = Math.min(w - 1, Math.max(0, Math.round((x - picture.left) * pointerScaleX)));
        int py = Math.min(h - 1, Math.max(0, Math.round((y - picture.top) * pointerScaleY)));
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

    private static int evdevButton(int button) {
        if (button == MotionEvent.BUTTON_PRIMARY) return 272;
        if (button == MotionEvent.BUTTON_SECONDARY) return 273;
        if (button == MotionEvent.BUTTON_TERTIARY) return 274;
        return 0;
    }

    @Override public boolean onCapturedPointerEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_HOVER_MOVE) {
            relative(event.getX(), event.getY());
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
        absolute(event.getX(), event.getY());
        if (a == MotionEvent.ACTION_DOWN) {
            primaryTouchDown = true;
            WaylandCompositor.nativeSendSceneInput(3, 272, 1);
        } else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
            if (primaryTouchDown) WaylandCompositor.nativeSendSceneInput(3, 272, 0);
            primaryTouchDown = false;
        }
        return true;
    }

    @Override public boolean onCheckIsTextEditor() { return true; }

    @Override public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI;
        return new BaseInputConnection(this, false) {
            @Override public boolean commitText(CharSequence text, int newCursorPosition) {
                if (ImeBridge.rootfulX11Active()) ImeBridge.commit(text.toString());
                else WaylandCompositor.textInputCommit(text.toString());
                return true;
            }
            @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
                if (!ImeBridge.rootfulX11Active()) {
                    WaylandCompositor.textInputPreedit(text.toString(), text.length());
                }
                // XTEST cannot transfer a preedit; insert only on commitText.
                return true;
            }
            @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                if (ImeBridge.rootfulX11Active()) ImeBridge.delete(beforeLength, afterLength);
                else WaylandCompositor.textInputDelete(beforeLength, afterLength);
                return true;
            }
        };
    }
}
