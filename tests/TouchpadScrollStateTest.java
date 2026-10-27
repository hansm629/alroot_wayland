import com.alroot.wayland.TouchpadScrollState;

public final class TouchpadScrollStateTest {
    static void check(boolean good, String why) {
        if (!good) throw new AssertionError(why);
    }
    public static void main(String[] args) {
        TouchpadScrollState touch = new TouchpadScrollState(7f, 1f);
        check(touch.move(2f) == 0 && !touch.isScrolling(),
                "Finger settling should not scroll");
        check(touch.move(3f) == 0 && !touch.isScrolling(),
                "Still within touch slop");
        int down = touch.move(40f);
        check(touch.isScrolling() && down < 0,
                "Downward fingers must produce negative vertical Wayland axis");
        touch.reset();
        int up = touch.move(-40f);
        check(touch.isScrolling() && up > 0,
                "Upward fingers must produce positive vertical Wayland axis");
        touch.reset();
        check(touch.move(3f) == 0 && !touch.isScrolling(),
                "Two-finger tap must not generate scrolling");
        System.out.println("PASS: touchpad natural scroll signs and tap dead zone");
    }
}
