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
            if (ms - recentAsciiMs > 2800) recentAscii = "";
            recentAscii += value;
            if (recentAscii.length() > 96)
                recentAscii = recentAscii.substring(recentAscii.length() - 96);
            recentAsciiMs = ms;
        }
    }

    public synchronized void composing(String incoming, long nowMs, Sink sink) {
        if (incoming == null) incoming = "";
        if (incoming.isEmpty()) {
            // The keyboard may be cancelling a draft on Backspace.
            draft = "";
            forwardedPrefix = "";
            return;
        }
        if (containsKorean(incoming) && !transitioning) {
            transitioning = true;
            koreanTransitionMs = nowMs;
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
        String text = stripPrefix(incoming, forwardedPrefix);
        if (text.isEmpty() && !draft.isEmpty() && incoming.isEmpty())
            text = draft;  // Samsung sometimes ends preedit with commitText("")

        if (transitioning && koreanTransitionMs >= 0 &&
                nowMs - koreanTransitionMs < 3000 &&
                recentAscii.length() >= 2 && text.startsWith(recentAscii)) {
            // Samsung keyboard can resend the already-committed English
            // run when switching to Korean. Remove only the exact recent
            // prefix in this narrow language-switch window.
            text = text.substring(recentAscii.length());
        }
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
        if (!draft.isEmpty()) emit(sink, draft, nowMs);
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

    public synchronized void reset() {
        draft = "";
        forwardedPrefix = "";
        transitioning = false;
        // Keep recent ASCII solely for a narrowly bounded language switch.
    }
}
