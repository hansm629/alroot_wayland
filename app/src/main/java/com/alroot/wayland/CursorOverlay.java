package com.alroot.wayland;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;

/** Visualizes the real Wayland cursor while Android mouse capture hides the system cursor. */
public final class CursorOverlay extends View {
    private final InputSurfaceView surface;
    private final android.graphics.Paint bitmapPaint =
            new android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG);
    private final Path startupArrow = new Path();
    private final Paint startupFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint startupOutline = new Paint(Paint.ANTI_ALIAS_FLAG);
    private Bitmap xfceStartupCursor;
    private int xfceHotspotX, xfceHotspotY;
    private Bitmap cursor;
    private int hotspotX, hotspotY;
    private boolean hidden;

    public CursorOverlay(Context context, InputSurfaceView surface) {
        super(context);
        this.surface = surface;
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        // A small fallback while native Labwc is publishing its initial
        // cursor. Do not apply device-density multiplication: that made the
        // previous P18 fallback conspicuously oversized.
        startupArrow.moveTo(0f, 0f);
        startupArrow.lineTo(0f, 22f);
        startupArrow.lineTo(5.5f, 16.5f);
        startupArrow.lineTo(11.5f, 27f);
        startupArrow.lineTo(15.5f, 25f);
        startupArrow.lineTo(9.5f, 14.5f);
        startupArrow.lineTo(19f, 14.5f);
        startupArrow.close();
        startupFill.setColor(android.graphics.Color.WHITE);
        startupOutline.setColor(android.graphics.Color.BLACK);
        startupOutline.setStyle(Paint.Style.STROKE);
        startupOutline.setStrokeWidth(1.5f);
        startupOutline.setStrokeJoin(Paint.Join.ROUND);
        refreshXfceStartupCursor();
    }

    /** The real XFCE cursor, not a hard-coded lookalike, before wl_pointer.set_cursor. */
    public void refreshXfceStartupCursor() {
        XcursorThemeLoader.Frame frame = XcursorThemeLoader.fromXfceSettings(
                new java.io.File("/data/data/com.termux/files/home"),
                new java.io.File("/data/data/com.termux/files/usr"));
        if (frame == null) {
            xfceStartupCursor = null;
            return;
        }
        Bitmap bitmap = Bitmap.createBitmap(frame.width, frame.height, Bitmap.Config.ARGB_8888);
        bitmap.setPixels(frame.pixels, 0, frame.width, 0, 0, frame.width, frame.height);
        bitmap.setDensity(Bitmap.DENSITY_NONE);
        xfceStartupCursor = bitmap;
        xfceHotspotX = frame.hotspotX;
        xfceHotspotY = frame.hotspotY;
        android.util.Log.i("AlrootWayland", "S22 startup Xcursor theme=" + frame.theme +
                " frame=" + frame.width + "x" + frame.height +
                " hotspot=" + frame.hotspotX + "," + frame.hotspotY);
        invalidate();
    }

    public void setGuestCursor(Bitmap bitmap, int hx, int hy, boolean isHidden) {
        cursor = bitmap;
        hotspotX = hx;
        hotspotY = hy;
        hidden = isHidden;
        invalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        // Keep the previous PiP behavior: no desktop pointer
        // painted over the small PiP preview during background viewing.
        if (getContext() instanceof android.app.Activity &&
                ((android.app.Activity)getContext()).isInPictureInPictureMode())
            return;
        if (!surface.isSoftwareCursorWanted() || hidden) return;
        float x = surface.pointerX(), y = surface.pointerY();
        canvas.save();
        canvas.translate(x, y);
        if (cursor != null) {
            canvas.scale(surface.cursorScaleX(), surface.cursorScaleY());
            canvas.drawBitmap(cursor, -hotspotX, -hotspotY, bitmapPaint);
        } else if (xfceStartupCursor != null) {
            // Same scene-to-view scale and hotspot as the incoming Xcursor
            // bitmap; no flash of an unrelated Wayland/Android arrow.
            canvas.scale(surface.cursorScaleX(), surface.cursorScaleY());
            canvas.drawBitmap(xfceStartupCursor, -xfceHotspotX, -xfceHotspotY, bitmapPaint);
        } else {
            float scale = PointerStartupPolicy.fallbackCursorScale(
                    Math.min(surface.cursorScaleX(), surface.cursorScaleY()));
            canvas.scale(scale, scale);
            canvas.drawPath(startupArrow, startupFill);
            canvas.drawPath(startupArrow, startupOutline);
        }
        canvas.restore();
    }
}
