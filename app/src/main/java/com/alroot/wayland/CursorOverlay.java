package com.alroot.wayland;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;

/** Visualizes the real Wayland cursor while Android mouse capture hides the system cursor. */
public final class CursorOverlay extends View {
    private final InputSurfaceView surface;
    private final android.graphics.Paint bitmapPaint =
            new android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG);
    private Bitmap cursor;
    private int hotspotX, hotspotY;
    private boolean hidden;

    public CursorOverlay(Context context, InputSurfaceView surface) {
        super(context);
        this.surface = surface;
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
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
        if (!surface.isSoftwareCursorWanted() || hidden) return;
        float x = surface.pointerX(), y = surface.pointerY();
        canvas.save();
        canvas.translate(x, y);
        if (cursor != null) {
            canvas.scale(surface.cursorScaleX(), surface.cursorScaleY());
            canvas.drawBitmap(cursor, -hotspotX, -hotspotY, bitmapPaint);
        }
        canvas.restore();
    }
}
