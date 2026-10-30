package com.alroot.wayland;

import java.util.Random;

/** JDK-only deterministic acceptance for Android View->guest XY mapping. */
public final class PointerSceneCoordinatePolicyTest {
    private static final int GUEST_W = 1920, GUEST_H = 1080;

    private static void expected(boolean result, String what) {
        if (!result) throw new AssertionError(what);
    }

    private static void check(int width, int height, int mode) {
        float factor = mode == 3
                ? Math.max(width / (float) GUEST_W, height / (float) GUEST_H)
                : mode == 2 ? 0f : Math.min(width / (float) GUEST_W, height / (float) GUEST_H);
        float pw = mode == 2 ? width : GUEST_W * factor;
        float ph = mode == 2 ? height : GUEST_H * factor;
        float lx = (width - pw) * .5f;
        float ly = (height - ph) * .5f;
        float sx = GUEST_W / pw;
        float sy = GUEST_H / ph;
        Random rand = new Random(629);
        float vx = lx + pw / 2f, vy = ly + ph / 2f;
        float worst = 0f;
        for (int i = 0; i < 50000; i++) {
            float deltaX = (float)(rand.nextGaussian() * 11);
            float deltaY = (float)(rand.nextGaussian() * 11);
            vx = Math.max(lx, Math.min(lx + pw - 1, vx + deltaX));
            vy = Math.max(ly, Math.min(ly + ph - 1, vy + deltaY));
            int x = PointerSceneCoordinatePolicy.guestCoordinate(vx, lx, sx, GUEST_W);
            int y = PointerSceneCoordinatePolicy.guestCoordinate(vy, ly, sy, GUEST_H);
            float error = Math.max(Math.abs(x - (vx - lx) * sx),
                                   Math.abs(y - (vy - ly) * sy));
            if (error > worst) worst = error;
            expected(error <= .501f, "mapping drift frame " + i + ": " + error);
            expected(x >= 0 && x < GUEST_W && y >= 0 && y < GUEST_H,
                    "out of range " + i);
        }
        System.out.println("S20_VIEW_TO_GUEST_50000[" + width + "x" + height +
                ",mode=" + mode + "]=PASS max_error=" + worst);
    }

    public static void main(String[] args) {
        check(3120, 1440, 1);
        check(2340, 1080, 1);
        check(1920, 1080, 1);
        check(752, 423, 1);
        check(3120, 1440, 2);
        check(3120, 1440, 3);
        expected(PointerSceneCoordinatePolicy.guestCoordinate(0, 280, .75f, 1920) == 0,
                "letterbox left clamp");
        expected(PointerSceneCoordinatePolicy.guestCoordinate(3120, 280, .75f, 1920) == 1919,
                "letterbox right clamp");
        expected(PointerSceneCoordinatePolicy.guestCoordinate(Float.NaN, 0, 1, 1920) == 0,
                "invalid x rejected");
        System.out.println("S20_VIEW_TO_GUEST_SCENE_MAPPING=PASS");
    }
}
