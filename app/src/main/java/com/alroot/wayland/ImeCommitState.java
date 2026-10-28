package com.alroot.wayland;

/**
 * State for an Android IME connected to a remote, text-unaware X11 window.
 *
 * XTEST can insert and delete text, but cannot display an XIM composition
 * range. Sending every mutable Hangul preedit as Backspace+Unicode produces
 * visible cursor flicker and may replace adjacent committed text.
 *
 * We therefore forward only finalized syllables or immutable prefixes.
 * Mutable preedit remains in the Android IME until a commit/finish event.
 * No clipboard, X11 selections, or speculative remote backspaces are used.
 *
 * Independent from Android APIs so regression tests run with regular javac.
 */
public final class ImeCommitState {
    private static final long ENGLISH_CONTEXT_WINDOW_MS = 60000;
    public interface Sink {
        void committed(String text);
        void delete(int before, int after);
    }

    private String draft = "";
    private String forwardedPrefix = "";
    private String recentAscii = "";
    private long recentAsciiMs;
    private long koreanTransitionMs = -1;
    private boolean transitioning;
    private String mostRecentFinish = "";
    private long mostRecentFinishMs = -1;
    private int suppressedEnglishEchoes;

    private static boolean containsKorean(String text) {
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if ((c >= 0x1100 && c <= 0x11ff) ||
                    (c >= 0x3130 && c <= 0x318f) ||
                    (c >= 0xac00 && c <= 0xd7af)) return true;
        }
        return false;
    }

    private static boolean ascii(String text) {
        for (int i = 0; i < text.length(); i++) {
            if (text.charAt(i) > 0x7f) return false;
        }
        return true;
    }

    private static int lead(char ch) {
        if (ch >= 0xac00 && ch <= 0xd7a3) return (ch - 0xac00) / 588;
        // Initial Hangul jamo and compatibility consonants, used to
        // distinguish ㄱ→가 (same syllable) from 가→나 (new syllable).
        String initials = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
        int index = initials.indexOf(ch);
        return index;
    }

    private static String stripPrefix(String text, String prefix) {
        return !prefix.isEmpty() && text.startsWith(prefix)
                ? text.substring(prefix.length()) : text;
    }

    private static String removeLastCodepoints(String text, int count) {
        int cps = text.codePointCount(0, text.length());
        int limit = Math.max(0, cps - Math.min(count, cps));
        return text.substring(0, text.offsetByCodePoints(0, limit));
    }

    private void emit(Sink sink, String value, long ms) {
        if (value.isEmpty()) return;
        sink.committed(value);
        if (ascii(value)) {
            recordAscii(value, ms);
        }
    }

    private void recordAscii(String text, long ms) {
        if (ms - recentAsciiMs > ENGLISH_CONTEXT_WINDOW_MS ||
                ms < recentAsciiMs) recentAscii = "";
        // One English word may be committed character by character by
        // Samsung Keyboard. Replacing this history with only the LAST
        // commit (P07) made "hello" appear twice on Hangul transition:
        // cached prefix was "o" instead of "hello".
        recentAscii += text;
        if (recentAscii.length() > 128)
            recentAscii = recentAscii.substring(recentAscii.length() - 128);
        recentAsciiMs = ms;
    }

    /**
     * ASCII composing is rendered immediately by the existing native X11
     * bridge (as in P06). Remember it for keyboard language-switch replay
     * filtering without inserting a second copy of the English text.
     */
    public synchronized void alreadyInsertedAscii(String text, long ms) {
        if (text == null || text.isEmpty() || !ascii(text)) return;
        recordAscii(text, ms);
    }

    /** Stale complete-word ASCII composing snapshot after a prior commit. */
    public synchronized boolean duplicateAsciiComposing(String text, long ms) {
        boolean duplicate = text != null && ascii(text) && text.length() >= 2 &&
                ms >= recentAsciiMs && ms - recentAsciiMs < 850 &&
                !recentAscii.isEmpty() && recentAscii.endsWith(text);
        if (duplicate) suppressedEnglishEchoes++;
        return duplicate;
    }

    /** Remove an echo only when its suffix also matches recent emitted ASCII. */
    private String trimEnglishEcho(String incoming, long nowMs) {
        if (!containsKorean(incoming) || recentAscii.length() < 2 ||
                nowMs < recentAsciiMs ||
                nowMs - recentAsciiMs > ENGLISH_CONTEXT_WINDOW_MS)
            return incoming;
        int length = Math.min(incoming.length(), recentAscii.length());
        for (int count = length; count >= 2; count--) {
            if (recentAscii.endsWith(incoming.substring(0, count)) &&
                    containsKorean(incoming.substring(count))) {
                suppressedEnglishEchoes++;
                return incoming.substring(count);
            }
        }
        return incoming;
    }

    /** Direct non-ASCII commits can arrive without a preceding preedit. */
    public synchronized void keyboardSwitchedToKorean(long nowMs) {
        transitioning = true;
        koreanTransitionMs = nowMs;
    }

    public synchronized void composing(String incoming, long nowMs, Sink sink) {
        if (incoming == null) incoming = "";
        if (containsKorean(incoming)) {
            transitioning = true;
            koreanTransitionMs = nowMs;
            incoming = trimEnglishEcho(incoming, nowMs);
        }
        if (!incoming.isEmpty()) mostRecentFinish = "";
        if (incoming.isEmpty()) {
            // The keyboard may be cancelling a draft on Backspace.
            draft = "";
            forwardedPrefix = "";
            return;
        }
        String next = stripPrefix(incoming, forwardedPrefix);
        if (next.equals(draft)) return;

        if (!draft.isEmpty() && !next.startsWith(draft)) {
            // A finished Hangul syllable followed by a different initial
            // can be forwarded now; a changing consonant/vowel of the
            // SAME syllable must stay buffered (no X11 backspace).
            if (draft.codePointCount(0, draft.length()) == 1 &&
                    next.codePointCount(0, next.length()) == 1 &&
                    draft.charAt(0) >= 0xac00 && draft.charAt(0) <= 0xd7a3 &&
                    containsKorean(next) &&
                    lead(draft.charAt(0)) != lead(next.charAt(0))) {
                emit(sink, draft, nowMs);
                forwardedPrefix += draft;
            }
        } else if (!draft.isEmpty() && next.startsWith(draft) &&
                next.length() > draft.length()) {
            // The keyboard is now composing a later syllable. The old
            // complete syllable is stable; the newest one stays in draft.
            int syllables = next.codePointCount(0, next.length());
            if (syllables > 1 && containsKorean(next)) {
                int prefixEnd = next.offsetByCodePoints(0, syllables - 1);
                String stable = next.substring(0, prefixEnd);
                emit(sink, stable, nowMs);
                forwardedPrefix += stable;
                next = next.substring(prefixEnd);
            }
        }
        draft = next;
    }

    public synchronized void committed(String incoming, long nowMs, Sink sink) {
        if (incoming == null) incoming = "";
        if (transitioning && !incoming.isEmpty() && ascii(incoming) &&
                incoming.length() >= 2 && recentAscii.endsWith(incoming) &&
                nowMs >= koreanTransitionMs && nowMs - koreanTransitionMs < 2800) {
            // Samsung can commit the English editor snapshot again after
            // the first Hangul composing event. This is an echo, not a new
            // user keystroke. Keep the Korean draft and do not send X11.
            suppressedEnglishEchoes++;
            return;
        }
        if (!incoming.isEmpty() && draft.isEmpty() &&
                incoming.equals(mostRecentFinish) && mostRecentFinishMs >= 0 &&
                nowMs >= mostRecentFinishMs && nowMs - mostRecentFinishMs < 600) {
            // Some keyboards send commitText even after finishComposingText
            // has already finalized that same glyph; do not duplicate it.
            mostRecentFinish = "";
            return;
        }
        mostRecentFinish = "";
        if (containsKorean(incoming)) {
            transitioning = true;
            if (koreanTransitionMs < 0) koreanTransitionMs = nowMs;
            incoming = trimEnglishEcho(incoming, nowMs);
        }
        String text = stripPrefix(incoming, forwardedPrefix);
        if (text.isEmpty() && !draft.isEmpty() && incoming.isEmpty())
            text = draft;  // Samsung sometimes ends preedit with commitText("")

        if (!text.isEmpty()) emit(sink, text, nowMs);
        if (text.isEmpty() && !incoming.isEmpty() && !draft.isEmpty()) {
            // An English echo is not a Korean finalization.
            return;
        }
        draft = "";
        forwardedPrefix = "";
        if (containsKorean(incoming) || incoming.isEmpty()) transitioning = false;
    }

    public synchronized void finished(long nowMs, Sink sink) {
        if (!draft.isEmpty()) {
            emit(sink, draft, nowMs);
            mostRecentFinish = draft;
            mostRecentFinishMs = nowMs;
        }
        draft = "";
        forwardedPrefix = "";
        transitioning = false;
    }

    /** True if deletion was consumed entirely by an uncommitted draft. */
    public synchronized boolean delete(int before, int after, Sink sink) {
        int b = Math.max(0, Math.min(128, before));
        int a = Math.max(0, Math.min(128, after));
        int pending = draft.codePointCount(0, draft.length());
        if (pending > 0 && b > 0) {
            draft = removeLastCodepoints(draft, b);
            b -= Math.min(pending, b);
        }
        if (b + a > 0) sink.delete(b, a);
        return pending > 0;
    }

    public synchronized boolean hasDraft() { return !draft.isEmpty(); }

    /** Diagnostics only. No text, hashes or clipboard data are exposed. */
    public synchronized int suppressedEchoCount() { return suppressedEnglishEchoes; }

    public synchronized void reset() {
        draft = "";
        forwardedPrefix = "";
        transitioning = false;
        // Keep recent ASCII solely for a narrowly bounded language switch.
    }

    public synchronized void clearEditorContext() {
        reset();
        recentAscii = "";
        recentAsciiMs = 0;
        koreanTransitionMs = -1;
        mostRecentFinish = "";
    }
}
