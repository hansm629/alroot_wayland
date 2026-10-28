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
    // Native Labwc supplies its cursor bitmap only after wl_pointer.enter.
    // During that interval show a small normal arrow at the *center*, not
    // Android's unrelated pointer at the top-left of the activity.
    private final Path startupArrow = new Path();
    private final Paint startupFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint startupOutline = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float startupScale;
    private Bitmap cursor;
    private int hotspotX, hotspotY;
    private boolean hidden;

    public CursorOverlay(Context context, InputSurfaceView surface) {
        super(context);
        this.surface = surface;
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        startupScale = Math.max(1f, getResources().getDisplayMetrics().density * 0.8f);
        startupArrow.moveTo(0f, 0f);
        startupArrow.lineTo(0f, 22f);
        startupArrow.lineTo(5.5f, 16.5f);
        startupArrow.lineTo(11.5f, 27f);
        startupArrow.lineTo(15.5f, 25f);
        startupArrow.lineTo(9.5f, 14.5f);
        startupArrow.lineTo(19f, 14.5f);
        startupArrow.close();
        startupFill.setStyle(Paint.Style.FILL);
        startupFill.setColor(android.graphics.Color.WHITE);
        startupOutline.setStyle(Paint.Style.STROKE);
        startupOutline.setStrokeWidth(1.8f);
        startupOutline.setStrokeJoin(Paint.Join.ROUND);
        startupOutline.setColor(android.graphics.Color.BLACK);
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
        // P18 makes touchpad's pointer visible from the first frame.
        // Keep the previous PiP behavior: no oversized desktop pointer
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
        } else {
            canvas.scale(startupScale, startupScale);
            canvas.drawPath(startupArrow, startupOutline);
            canvas.drawPath(startupArrow, startupFill);
        }
        canvas.restore();
    }
}
