package com.alroot.wayland;

/**
 * Remote X11 text bridge state. Unlike P07/P08, EVERY mutable Korean preedit
 * is forwarded immediately as the native daemon's 'C' operation.
 *
 * The native daemon already supports in-place C replacement by common-prefix
 * XTEST Backspace/Unicode keys, closely matching Termux-X11 replaceText().
 * Delaying until a later T/finish operation made Samsung Hangul and DEL
 * appear frozen. No clipboard, process fork or timed debounce is involved.
 *
 * No Android dependencies so event sequences can be regression-tested by
 * the same JVM class used by the production Android application.
 */
public final class ImeCommitState {
    public interface Sink {
        void composing(String text);
        void committed(String text);
        void delete(int before, int after);
    }

    private static final long ENGLISH_CONTEXT_MS = 60000;
    private static final long ECHO_WINDOW_MS = 1200;
    private String draft = "";
    private String recentAscii = "";
    private long recentAsciiAt = -1;
    private boolean koreanTransition;
    private String lastFinished = "";
    private long lastFinishedMs = -1;
    private boolean finishEchoPending;
    private int echoSuppressions;
    private int emittedCompositions;
    private int emittedCommits;

    private static boolean korean(String text) {
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if ((c >= 0x1100 && c <= 0x11ff) ||
                (c >= 0x3130 && c <= 0x318f) ||
                (c >= 0xac00 && c <= 0xd7af)) return true;
        }
        return false;
    }
    private static boolean ascii(String text) {
        if (text == null) return false;
        for (int i = 0; i < text.length(); i++)
            if (text.charAt(i) > 127) return false;
        return true;
    }
    private static String subtractCodepoints(String value, int before) {
        int length = value.codePointCount(0, value.length());
        int remaining = Math.max(0, length - Math.min(length, before));
        return value.substring(0, value.offsetByCodePoints(0, remaining));
    }
    private static int initialHangulConsonant(String value) {
        if (value == null || value.codePointCount(0, value.length()) != 1)
            return -1;
        int cp = value.codePointAt(0);
        if (cp >= 0xac00 && cp <= 0xd7a3)
            return (cp - 0xac00) / 588;
        return -1;
    }
    private void rememberAscii(String text, long ms) {
        if (text == null || text.isEmpty() || !ascii(text)) return;
        if (recentAsciiAt < 0 || ms < recentAsciiAt ||
                ms - recentAsciiAt > ENGLISH_CONTEXT_MS)
            recentAscii = "";
        recentAscii += text;
        if (recentAscii.length() > 128)
            recentAscii = recentAscii.substring(recentAscii.length() - 128);
        recentAsciiAt = ms;
    }
    public synchronized void alreadyInsertedAscii(String text, long ms) {
        rememberAscii(text, ms);
    }
    public synchronized boolean duplicateAsciiComposing(String text, long ms) {
        boolean echo = text != null && text.length() >= 2 && ascii(text)
                && recentAsciiAt >= 0 && ms >= recentAsciiAt
                && ms - recentAsciiAt < ECHO_WINDOW_MS
                && recentAscii.endsWith(text);
        if (echo) echoSuppressions++;
        return echo;
    }
    public synchronized void keyboardSwitchedToKorean(long ms) {
        koreanTransition = true;
    }
    private String removeEnglishEcho(String text, long ms) {
        if (!korean(text) || recentAsciiAt < 0 || ms < recentAsciiAt ||
                ms - recentAsciiAt > ENGLISH_CONTEXT_MS) return text;
        // Android IME may retransmit an already-committed English run as
        // surrounding editor context. Remove the longest *exact* suffix of
        // text we actually delivered, but only if followed by Hangul.
        int limit = Math.min(text.length(), recentAscii.length());
        for (int n = limit; n >= 2; n--) {
            if (recentAscii.endsWith(text.substring(0, n)) &&
                    korean(text.substring(n))) {
                echoSuppressions++;
                return text.substring(n);
            }
        }
        return text;
    }
    public synchronized void composing(String text, long ms, Sink sink) {
        if (text == null) text = "";
        if (korean(text)) koreanTransition = true;
        String normalized = removeEnglishEcho(text, ms);
        if (normalized.equals(draft)) return;
        int priorInitial = initialHangulConsonant(draft);
        int nextInitial = initialHangulConsonant(normalized);
        if (priorInitial >= 0 && nextInitial >= 0 &&
                priorInitial != nextInitial) {
            // Some Samsung versions replace the composing span when the
            // previous syllable is finalized and the next one starts,
            // without an intervening commitText. Keep the previous
            // syllable, then immediately show the new preedit.
            emittedCommits++;
            sink.committed(draft);
            draft = "";
        }
        draft = normalized;
        finishEchoPending = false;
        emittedCompositions++;
        sink.composing(normalized); // immediate 'C' to the X11 native socket
    }
    public synchronized void committed(String text, long ms, Sink sink) {
        if (text == null) text = "";
        if (korean(text)) koreanTransition = true;
        if (koreanTransition && !text.isEmpty() && ascii(text) &&
                text.length() >= 2 && recentAscii.endsWith(text)) {
            echoSuppressions++; // stale English commit during 한/영 switch
            return;
        }
        String normalized = removeEnglishEcho(text, ms);
        if (normalized.isEmpty() && text.isEmpty() && !draft.isEmpty())
            normalized = draft; // Samsung commitText("") finishes preedit

        if (finishEchoPending && draft.isEmpty() &&
                normalized.equals(lastFinished) && lastFinishedMs >= 0 &&
                ms >= lastFinishedMs && ms - lastFinishedMs < 400) {
            // Some IMEs redundantly commit immediately after explicit finish.
            echoSuppressions++;
            finishEchoPending = false;
            return;
        }

        // 'T' tells the native daemon to finalize *the existing composing
        // content*. It does not insert a second copy if C already displayed it.
        if (!normalized.isEmpty() || !draft.isEmpty()) {
            emittedCommits++;
            sink.committed(normalized);
            if (ascii(normalized)) rememberAscii(normalized, ms);
        }
        draft = "";
        koreanTransition = false;
        finishEchoPending = false;
    }
    public synchronized void finished(long ms, Sink sink) {
        if (!draft.isEmpty()) {
            emittedCommits++;
            sink.committed(draft);
            lastFinished = draft;
            lastFinishedMs = ms;
            finishEchoPending = true;
        }
        draft = "";
        koreanTransition = false;
    }
    public synchronized boolean delete(int before, int after, Sink sink) {
        int b = Math.max(0, Math.min(before, 128));
        int a = Math.max(0, Math.min(after, 128));
        int pending = draft.codePointCount(0, draft.length());
        if (pending > 0 && b > 0) {
            // The composing glyph is ALREADY visible in X11. Replacing it
            // immediately by C(new text) keeps DEL responsive. A buffered-
            // only deletion was the main P07/P08 user-visible delay.
            draft = subtractCodepoints(draft, b);
            emittedCompositions++;
            sink.composing(draft);
            b -= Math.min(b, pending);
        }
        if (b + a > 0) sink.delete(b, a);
        return pending > 0;
    }
    public synchronized boolean hasDraft() { return !draft.isEmpty(); }
    public synchronized int suppressedEchoCount() { return echoSuppressions; }
    public synchronized int composingCount() { return emittedCompositions; }
    public synchronized int committedCount() { return emittedCommits; }
    public synchronized void reset() {
        draft = "";
        koreanTransition = false;
        finishEchoPending = false;
    }
    public synchronized void clearEditorContext() {
        reset();
        recentAscii = "";
        recentAsciiAt = -1;
        lastFinished = "";
        lastFinishedMs = -1;
    }
}
