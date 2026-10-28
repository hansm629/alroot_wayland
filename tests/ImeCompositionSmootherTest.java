import com.alroot.wayland.ImeCompositionSmoother;
import java.util.ArrayList;
import java.util.List;

public final class ImeCompositionSmootherTest {
    static void check(boolean yes, String description) {
        if (!yes) throw new AssertionError(description);
    }
    public static void main(String[] args) {
        ImeCompositionSmoother smoother = new ImeCompositionSmoother();
        List<String> output = new ArrayList<>();
        ImeCompositionSmoother.Output emit = output::add;

        check(smoother.deletedRevision("", emit), "Empty deletion must wait briefly");
        check(output.isEmpty(), "No intermediate blank glyph sent");
        smoother.replaced("하", emit);
        check(output.size() == 1 && output.get(0).equals("하"),
                "Compose replaces old glyph without empty frame");
        check(!smoother.flush(emit), "Do not erase after replacement");

        check(smoother.deletedRevision("", emit), "Second empty deletion pending");
        check(smoother.flush(emit), "Single Backspace must still erase");
        check(output.size() == 2 && output.get(1).isEmpty(),
                "Standalone Backspace erases once");
        check(!smoother.flush(emit), "Do not duplicate an erase event");

        check(!smoother.deletedRevision("한", emit), "Nonempty delete is immediate");
        check(output.size() == 3 && output.get(2).equals("한"),
                "Shorter preedit not delayed");

        check(smoother.deletedRevision("", emit), "Pending before a commit");
        smoother.cancel();
        check(!smoother.flush(emit), "Commit must not trigger late empty erase");
        check(output.size() == 3, "No stale out-of-order erase after commit");
        System.out.println("PASS: 10 Korean deletion frame-coalescing assertions");
    }
}
