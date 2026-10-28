import com.alroot.wayland.ImeBackspaceGuard;

public final class ImeBackspaceGuardTest {
    static void check(boolean valid, String description) {
        if (!valid) throw new AssertionError(description);
    }
    public static void main(String[] args) {
        ImeBackspaceGuard guard = new ImeBackspaceGuard();
        check(!guard.isEcho(ImeBackspaceGuard.KEY_EVENT, 100),
                "First DEL key must execute");
        check(guard.isEcho(ImeBackspaceGuard.SURROUNDING_TEXT, 108),
                "IME repeated DEL as deleteSurroundingText must be suppressed");
        check(!guard.isEcho(ImeBackspaceGuard.KEY_EVENT, 240),
                "A later physical Backspace is a new action");
        check(!guard.isEcho(ImeBackspaceGuard.KEY_EVENT, 260),
                "Repeat events of same kind must remain responsive");
        check(!guard.isEcho(ImeBackspaceGuard.SURROUNDING_TEXT, 500),
                "A separate surrounding-text delete is legitimate");
        check(guard.isEcho(ImeBackspaceGuard.KEY_EVENT, 517),
                "KEY_EVENT echo after surrounding-text DEL must be ignored");
        guard.reset();
        check(!guard.isEcho(ImeBackspaceGuard.KEY_EVENT, 525),
                "Focus changes must reset the suppression window");
        guard.reset();
        // A synthetic cursor cleanup can be followed by Samsung's
        // KEYCODE_DEL for the same phantom space. Neither reaches X11.
        check(!guard.isEcho(ImeBackspaceGuard.SURROUNDING_TEXT, 1000),
                "Phantom surrounding cleanup starts a guard window");
        check(guard.isEcho(ImeBackspaceGuard.KEY_EVENT, 1008),
                "Companion phantom key event must be suppressed");
        check(!guard.isEcho(ImeBackspaceGuard.KEY_EVENT, 1160),
                "A separate user Backspace later must still work");
        System.out.println("PASS: 10 Backspace ordering and phantom cleanup checks");
    }
}
