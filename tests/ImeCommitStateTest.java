import com.alroot.wayland.ImeCommitState;

/**
 * Android-independent state regression corpus. Tests the actual Java logic
 * used by Samsung/Gboard InputConnection, without touching a live keyboard.
 */
public final class ImeCommitStateTest {
    static class FakeSink implements ImeCommitState.Sink {
        StringBuilder text = new StringBuilder();
        int mutations, deletions;
        @Override public void committed(String value) {
            text.append(value);
            mutations++;
        }
        @Override public void delete(int before, int after) {
            int cps = text.codePointCount(0, text.length());
            int remove = Math.min(cps, before);
            text.setLength(text.offsetByCodePoints(text.length(), -remove));
            deletions += before + after;
            mutations++;
        }
        void check(String expected) {
            if (!expected.contentEquals(text))
                throw new AssertionError("expected=" + expected + " actual=" + text);
        }
    }
    static void check(boolean valid, String desc) {
        if (!valid) throw new AssertionError(desc);
    }
    static void englishHangulSwitch() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.committed("hello", 100, o);
        s.composing("ㅎ", 130, o);
        s.committed("hello", 145, o); // Samsung stale English echo
        s.composing("하", 150, o);
        s.composing("한", 155, o);
        s.committed("한", 166, o);
        o.check("hello한");
        check(o.deletions == 0, "English switch must not delete remote text");
    }
    static void hangulEvolutionNoFlicker() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.composing("ㅎ", 300, o);
        s.composing("하", 320, o);
        s.composing("한", 340, o);
        check(o.mutations == 0, "No X11 repaints while glyph is mutable");
        s.committed("한", 350, o);
        o.check("한");
        check(o.mutations == 1 && o.deletions == 0, "One atomic commit");
    }
    static void syllablesWithoutExplicitCommit() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.composing("가", 400, o);
        s.composing("나", 410, o);
        o.check("가");
        s.finished(420, o);
        o.check("가나");
        check(o.deletions == 0, "No remote speculative deletion");
    }
    static void multipleTextSnapshot() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.composing("가", 100, o);
        s.composing("가나", 110, o);
        o.check("가");
        s.composing("가나다", 120, o);
        o.check("가나");
        s.committed("가나다", 130, o);
        o.check("가나다");
        check(o.deletions == 0, "No remapping/backspace flicker");
    }
    static void deletingMutableText() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.composing("각", 130, o);
        check(s.delete(1, 0, o), "pending syllable consumed locally");
        s.composing("", 131, o);
        s.finished(135, o);
        o.check("");
        check(o.mutations == 0, "No remote Backspace for uncommitted draft");
        s.committed("가", 140, o);
        check(!s.delete(1, 0, o), "after commit delete remote");
        o.check("");
        check(o.deletions == 1, "Exactly one X11 Backspace");
    }
    static void emptySamsungCommit() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.composing("한", 90, o);
        s.committed("", 92, o);
        s.finished(93, o);
        o.check("한");
        check(o.mutations == 1, "No duplicate after empty commit");
    }
    static void languagePrefixEcho() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.committed("cat", 100, o);
        s.composing("한", 120, o);
        s.committed("cat한", 130, o);
        o.check("cat한");
    }
    static void legitimateEnglishRepeat() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.committed("hi", 100, o);
        s.committed("hi", 120, o);
        o.check("hihi");
    }
    static void differentVowelSameSyllable() {
        ImeCommitState s = new ImeCommitState();
        FakeSink o = new FakeSink();
        s.composing("가", 30, o);
        s.composing("갸", 40, o);
        s.committed("갸", 60, o);
        o.check("갸");
        check(o.deletions == 0, "Do not replace typed syllables in place");
    }
    public static void main(String[] args) {
        englishHangulSwitch();
        hangulEvolutionNoFlicker();
        syllablesWithoutExplicitCommit();
        multipleTextSnapshot();
        deletingMutableText();
        emptySamsungCommit();
        languagePrefixEcho();
        legitimateEnglishRepeat();
        differentVowelSameSyllable();
        System.out.println("PASS: 9 real ImeCommitState scenarios; no destructive preedit");
    }
}
