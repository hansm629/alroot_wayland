import com.alroot.wayland.ImeCommitState;

/**
 * Same 'C' (replace composing suffix), 'T' (finalize), 'D' protocol as the
 * production native X11 input daemon. No user data or live X11 display.
 */
public final class ImeCommitStateTest {
    static class FakeNative implements ImeCommitState.Sink {
        final StringBuilder rendered = new StringBuilder();
        String composing = "";
        int preeditWrites, commits, deletions;
        int firstPreeditObservedAt = -1;

        private void erasePreedit() {
            int oldLength = composing.length();
            if (oldLength > 0) {
                if (!rendered.toString().endsWith(composing))
                    throw new AssertionError("Composing text no longer matches X11 suffix");
                rendered.setLength(rendered.length() - oldLength);
            }
            composing = "";
        }
        @Override public void composing(String text) {
            erasePreedit();
            rendered.append(text);
            composing = text;
            preeditWrites++;
            if (firstPreeditObservedAt == -1) firstPreeditObservedAt = preeditWrites;
        }
        @Override public void committed(String text) {
            erasePreedit();
            rendered.append(text);
            commits++;
        }
        @Override public void delete(int before, int after) {
            if (!composing.isEmpty())
                throw new AssertionError("Remote committed delete while draft is present");
            int cps = rendered.codePointCount(0, rendered.length());
            int toDelete = Math.min(cps, before);
            rendered.setLength(rendered.offsetByCodePoints(rendered.length(), -toDelete));
            deletions += before + after;
        }
        void check(String expected) {
            if (!expected.contentEquals(rendered))
                throw new AssertionError("expected=" + expected + " actual=" + rendered);
        }
    }
    static void check(boolean good, String why) {
        if (!good) throw new AssertionError(why);
    }

    static void koreanIsVisibleAtFirstPreedit() {
        ImeCommitState state = new ImeCommitState();
        FakeNative nativeX11 = new FakeNative();
        state.composing("ㅎ", 0, nativeX11);
        nativeX11.check("ㅎ"); // P08 FAIL: remained empty until a later commit
        check(nativeX11.preeditWrites == 1, "First character must reach X11 immediately");
        state.composing("하", 10, nativeX11);
        nativeX11.check("하");
        state.composing("한", 20, nativeX11);
        nativeX11.check("한");
        state.committed("한", 30, nativeX11);
        nativeX11.check("한");
        check(nativeX11.deletions == 0, "Remote committed text not deleted");
        check(state.composingCount() == 3, "All changed preedits were forwarded");
    }

    static void koreanBackspaceIsImmediate() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        state.composing("한국", 100, x);
        x.check("한국");
        check(state.delete(1, 0, x), "Pending preedit should be deleted locally");
        x.check("한"); // P08 FAIL: X11 still showed nothing
        check(state.delete(1, 0, x), "Second backspace should remove remaining composing text");
        x.check("");
        check(x.deletions == 0, "Only C replacements, not blind D");
    }

    static void englishHangulSwitch() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.committed("hello", 100, o);
        s.composing("ㅎ", 130, o);
        o.check("helloㅎ");
        s.committed("hello", 145, o); // Samsung stale English snapshot
        o.check("helloㅎ");
        s.composing("하", 150, o);
        o.check("hello하");
        s.composing("한", 155, o);
        s.committed("한", 166, o);
        o.check("hello한");
        check(o.deletions == 0, "English switch must not delete committed text");
    }

    static void syllablesWithoutExplicitCommit() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.composing("가", 400, o);
        o.check("가");
        s.composing("나", 410, o);
        o.check("가나");
        s.finished(420, o);
        o.check("가나");
    }

    static void multiSyllableSnapshot() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.composing("가", 100, o);
        s.composing("가나", 110, o);
        o.check("가나");
        s.composing("가나다", 120, o);
        o.check("가나다");
        s.committed("가나다", 130, o);
        o.check("가나다");
        check(o.deletions == 0, "No committed deletion");
    }

    static void emptySamsungCommit() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.composing("한", 90, o);
        s.committed("", 92, o);
        s.finished(93, o);
        o.check("한");
        check(o.commits == 1, "No duplicate finalization");
    }

    static void languagePrefixEcho() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.committed("cat", 100, o);
        s.composing("catㅎ", 120, o);
        o.check("catㅎ");
        s.committed("cat한", 130, o);
        o.check("cat한");
    }

    static void legitimateEnglishRepeat() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.committed("hi", 100, o);
        s.committed("hi", 120, o);
        o.check("hihi");
    }

    static void vowelChangesAreReplacements() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.composing("가", 30, o);
        s.composing("갸", 40, o);
        o.check("갸");
        s.committed("갸", 60, o);
        o.check("갸");
    }

    static void finishThenRepeatedCommit() {
        ImeCommitState s = new ImeCommitState();
        FakeNative o = new FakeNative();
        s.composing("한", 100, o);
        s.finished(130, o);
        s.committed("한", 150, o);
        o.check("한");
        check(o.commits == 1, "No post-finish duplicate");
    }

    static void directKoreanCommitAfterAscii() {
        ImeCommitState state = new ImeCommitState();
        FakeNative out = new FakeNative();
        out.committed("hello"); // inserted by ImeBridge native ASCII C/T
        state.alreadyInsertedAscii("hello", 100);
        state.keyboardSwitchedToKorean(150);
        state.committed("hello한", 160, out);
        out.check("hello한");
    }

    static void chunkedEnglishHangulPreedit() {
        ImeCommitState state = new ImeCommitState();
        FakeNative out = new FakeNative();
        for (String value : new String[]{"h","e","l","l","o"}) {
            out.committed(value);
            state.alreadyInsertedAscii(value, 80 + out.rendered.length() * 20);
        }
        state.composing("helloㅎ", 260, out);
        out.check("helloㅎ");
        state.composing("hello하", 275, out);
        out.check("hello하");
        state.composing("hello한", 280, out);
        state.committed("hello한", 300, out);
        out.check("hello한");
    }

    static void chunkedEnglishThenDirectKoreanCommit() {
        ImeCommitState state = new ImeCommitState();
        FakeNative out = new FakeNative();
        for (String value : new String[]{"t", "e", "s", "t"}) {
            out.committed(value);
            state.alreadyInsertedAscii(value, 100 + out.rendered.length() * 10);
        }
        state.committed("test한", 240, out);
        out.check("test한");
    }

    static void englishSuffixEchoOnly() {
        ImeCommitState state = new ImeCommitState();
        FakeNative out = new FakeNative();
        out.committed("welcome hello");
        state.alreadyInsertedAscii("welcome hello", 300);
        state.composing("helloㅎ", 310, out);
        state.committed("hello한", 340, out);
        out.check("welcome hello한");
    }

    static void staleAsciiSnapshotDetected() {
        ImeCommitState state = new ImeCommitState();
        state.alreadyInsertedAscii("hello", 100);
        check(state.duplicateAsciiComposing("hello", 140), "Stale complete English word");
        check(!state.duplicateAsciiComposing("hell", 140), "Do not drop a partial prefix");
        check(!state.duplicateAsciiComposing("h", 140), "Do not drop legitimate single letter");
        check(!state.duplicateAsciiComposing("hello", 1600), "Stale echo timeout");
    }

    static void delayedLanguageSwitchAndFocusReset() {
        ImeCommitState state = new ImeCommitState();
        FakeNative result = new FakeNative();
        result.committed("hello");
        state.alreadyInsertedAscii("hello", 100);
        state.composing("helloㅎ", 12000, result);
        state.committed("hello한", 12100, result);
        result.check("hello한");
        state.clearEditorContext();
        result.committed("hello한");
        state.composing("helloㅎ", 14000, result);
        state.committed("hello한", 14010, result);
        result.check("hello한hello한hello한");
    }

    static void manyUniqueSyllablesRealtime() {
        ImeCommitState state = new ImeCommitState();
        FakeNative nativeX11 = new FakeNative();
        StringBuilder expected = new StringBuilder();
        for (int i = 0; i < 24; i++) {
            String value = new String(Character.toChars(0xac00 + i));
            state.composing(value, i * 15, nativeX11);
            expected.append(value);
            nativeX11.check(expected.toString());
            state.committed(value, i * 15 + 5, nativeX11);
            nativeX11.check(expected.toString());
        }
        check(nativeX11.preeditWrites == 24,
                "24 preedit messages each delivered before commit");
    }

    static void deleteCommittedAfterComposing() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        state.composing("한", 0, x);
        state.committed("한", 10, x);
        check(!state.delete(1, 0, x), "Committed text requires X11 D");
        x.check("");
        check(x.deletions == 1, "One remote Backspace for committed text");
    }

    static void canceledCompositionRemovesX11Only() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        x.committed("hello");
        state.composing("안", 100, x);
        state.composing("", 110, x);
        x.check("hello");
    }

    static void twoWordDirectPreeditSnapshot() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        state.composing("한", 100, x);
        state.composing("한국", 110, x);
        x.check("한국");
        state.composing("한국어", 130, x);
        x.check("한국어");
        state.committed("한국어", 150, x);
        x.check("한국어");
    }

    // P12 regression: after editing English then Korean and deleting the
    // Korean syllable, Samsung can replay its earlier English snapshot.
    // The old generic 1200ms filter expired during normal Korean typing.
    static void hangulDeletedThenStaleEnglishSnapshot() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        state.committed("hello", 100, x);
        state.composing("한", 5000, x);
        state.committed("한", 5100, x);
        x.check("hello한");
        state.backspace(5105);
        state.delete(1, 0, x);
        x.check("hello");
        check(state.duplicateAsciiComposing("hello", 5110),
              "Stale English composing after Hangul DEL must be detected");
        state.committed("hello", 5120, x);
        x.check("hello"); // no automatic hellohello
    }

    static void normalEnglishAfterHangulDeleteIsNotLost() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        state.committed("hello", 100, x);
        state.composing("한", 5000, x);
        state.committed("한", 5100, x);
        state.backspace(5105);
        state.delete(1, 0, x);
        // Genuine user-typed ASCII letters arrive as single/partial changes.
        check(!state.duplicateAsciiComposing("h", 5110),
              "Never discard a new single Latin letter");
        state.committed("h", 5120, x);
        x.check("helloh");
    }

    static void finishedHangulThenDeleteAndEnglishEcho() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        state.committed("hello", 100, x);
        state.composing("한", 3200, x);
        state.finished(3250, x); // Samsung uses finishComposingText
        x.check("hello한");
        state.backspace(3300);
        state.delete(1, 0, x);
        check(state.duplicateAsciiComposing("hello", 3330),
                "Post-finish Hangul deletion must arm stale English guard");
        state.committed("hello", 3340, x);
        x.check("hello");
    }

    static void newEditorDoesNotSuppressLegitimateEnglish() {
        ImeCommitState state = new ImeCommitState();
        FakeNative x = new FakeNative();
        state.committed("hello", 100, x);
        state.composing("한", 3200, x);
        state.committed("한", 3210, x);
        state.backspace(3220);
        state.delete(1, 0, x);
        state.clearEditorContext();
        check(!state.duplicateAsciiComposing("hello", 3250),
                "New editor focus must clear old English echo state");
        state.committed("hello", 3260, x);
        x.check("hellohello");
    }

    public static void main(String[] args) {
        koreanIsVisibleAtFirstPreedit();
        koreanBackspaceIsImmediate();
        englishHangulSwitch();
        syllablesWithoutExplicitCommit();
        multiSyllableSnapshot();
        emptySamsungCommit();
        languagePrefixEcho();
        legitimateEnglishRepeat();
        vowelChangesAreReplacements();
        finishThenRepeatedCommit();
        directKoreanCommitAfterAscii();
        chunkedEnglishHangulPreedit();
        chunkedEnglishThenDirectKoreanCommit();
        englishSuffixEchoOnly();
        staleAsciiSnapshotDetected();
        delayedLanguageSwitchAndFocusReset();
        manyUniqueSyllablesRealtime();
        deleteCommittedAfterComposing();
        canceledCompositionRemovesX11Only();
        twoWordDirectPreeditSnapshot();
        hangulDeletedThenStaleEnglishSnapshot();
        normalEnglishAfterHangulDeleteIsNotLost();
        finishedHangulThenDeleteAndEnglishEcho();
        newEditorDoesNotSuppressLegitimateEnglish();
        System.out.println("PASS: 24 Android IME event scenarios, Korean DEL and English echo");
    }
}
