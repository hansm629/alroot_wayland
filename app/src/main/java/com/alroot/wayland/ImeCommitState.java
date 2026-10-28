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
    // Samsung's stale Latin snapshot can repeat for >10 seconds after
    // Hangul deletion (observed every ~50ms on S26U). An absolute timeout
    // from the initial Backspace expires while it is still actively
    // replaying. Refresh protection upon each identified old snapshot,
    // but allow intentional same-word typing after a quiet period.
    private static final long STALE_REPLAY_QUIET_MS = 5000;
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
    private boolean lastCommittedWasKorean;
    private long lastKoreanDeleteAt = -1;
    private long lastStaleReplayAt = -1;
    private boolean mayReplayEnglishAfterKoreanDelete;
    // Samsung may clear native Hangul preedit with C("") *before* its DEL.
    // Without remembering that short-lived transition the DEL erases the
    // preceding committed English instead, and stale IME context can replay.
    private long clearedKoreanPreeditAt = -1;
    private boolean clearedKoreanPreedit;
    private boolean consumeClearedPreeditDelete;
    private long lastKoreanActivityAt = -1;
    // The Android fake editor has no real English backing text. Following
    // Hangul DEL Samsung may rebuild the *previous* Latin run one prefix at
    // a time ("h" -> "he" -> "hel" ...). Never send that speculative C to X11
    // until a genuine new ASCII commit distinguishes it from cached context.
    private String withheldAscii = "";
    // P20 (captured on real S26U): Samsung's setComposingText first contains
    // already-committed English plus Korean. When the final Korean syllable
    // is deleted the same *old English prefix* becomes the whole composing
    // value, and shrinks one character at a time. Unlike the older DEL
    // filters, this guard is armed while Korean is STILL composing.
    // Track the exact prefix that we stripped from mixed C, not arbitrary
    // previously typed ASCII, so unrelated fresh English is unaffected.
    private String mixedContextPrefix = "";
    private boolean drainingMixedContext;
    private String drainedMixedContextCommit = "";
    private static final long EMPTY_KOREAN_DELETE_MS = 120;
    private static final long KOREAN_CONTEXT_MS = 3000;

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
    /**
     * A software keyboard can deliver English using scanless KeyEvents,
     * bypassing InputConnection.commitText entirely. The X11 guest has
     * already received that physical-style key, so this method records ONLY
     * the context used to recognize subsequent Samsung stale composition;
     * it does not emit another character.
     */
    public synchronized void alreadyInsertedDirectAscii(String text, long ms) {
        if (text == null || text.isEmpty() || !ascii(text)) return;
        rememberAscii(text, ms);
        mayReplayEnglishAfterKoreanDelete = false;
        lastStaleReplayAt = -1;
        withheldAscii = "";
        koreanTransition = false;
        lastCommittedWasKorean = false;
        mixedContextPrefix = "";
        drainingMixedContext = false;
        drainedMixedContextCommit = "";
    }
    public synchronized boolean duplicateAsciiComposing(String text, long ms) {
        boolean sameOldText = text != null && text.length() >= 2 &&
                ascii(text) && !recentAscii.isEmpty() && recentAscii.endsWith(text);
        boolean initialEcho = sameOldText && recentAsciiAt >= 0 &&
                ms >= recentAsciiAt && ms - recentAsciiAt < ECHO_WINDOW_MS;
        boolean afterDeletionEcho = sameOldText && isPostKoreanDeletion(ms);
        boolean echo = initialEcho || afterDeletionEcho;
        if (echo) {
            echoSuppressions++;
            if (afterDeletionEcho) lastStaleReplayAt = ms;
        }
        return echo;
    }
    private boolean isPostKoreanDeletion(long nowMs) {
        long latest = Math.max(lastKoreanDeleteAt, lastStaleReplayAt);
        return mayReplayEnglishAfterKoreanDelete &&
                latest >= 0 && nowMs >= latest &&
                nowMs - latest <= STALE_REPLAY_QUIET_MS;
    }
    private boolean matchesPriorAscii(String text) {
        return text != null && !text.isEmpty() && ascii(text) &&
                !recentAscii.isEmpty() &&
                (recentAscii.startsWith(text) || recentAscii.endsWith(text));
    }
    private boolean inKoreanDeletionBoundary(long ms) {
        return isPostKoreanDeletion(ms) || hasRecentClearedKoreanPreedit(ms);
    }
    /**
     * Hold only a possible old ASCII snapshot from Samsung after a Korean
     * edit; ordinary English C/T and native Hangul C/T remain immediate.
     */
    public synchronized boolean withholdSuspectedAscii(String text, long ms) {
        if (inKoreanDeletionBoundary(ms) && matchesPriorAscii(text)) {
            withheldAscii = text;
            lastStaleReplayAt = ms;
            return true;
        }
        withheldAscii = "";
        return false;
    }
    public synchronized boolean suppressWithheldAsciiCommit(String text, long ms) {
        boolean had = !withheldAscii.isEmpty();
        withheldAscii = "";
        if (had && text != null && text.length() >= 2 &&
                inKoreanDeletionBoundary(ms) && matchesPriorAscii(text)) {
            lastStaleReplayAt = ms;
            echoSuppressions++;
            return true;
        }
        return false;
    }
    /** Preserve genuine single-character English if the IME finishes it. */
    public synchronized String takeWithheldAsciiOnFinish(long ms) {
        String value = withheldAscii;
        withheldAscii = "";
        if (!value.isEmpty() && inKoreanDeletionBoundary(ms) &&
                matchesPriorAscii(value)) {
            // This prefix has NOT been accepted from a genuine commitText.
            // Android calls finishComposingText also for Back/focus/keypad
            // transitions, so force-committing even one old Latin character
            // here silently reinserts text the user did not type.
            echoSuppressions++;
            lastStaleReplayAt = ms;
            return "";
        }
        return value;
    }
    public synchronized void discardWithheldAscii() { withheldAscii = ""; }
    /** Called for either Android deletion API, before the remote Backspace. */
    public synchronized void backspace(long nowMs) {
        if (lastCommittedWasKorean || korean(draft) ||
                (lastKoreanActivityAt >= 0 && nowMs >= lastKoreanActivityAt &&
                nowMs - lastKoreanActivityAt <= KOREAN_CONTEXT_MS)) {
            lastKoreanDeleteAt = nowMs;
            lastStaleReplayAt = -1;
            mayReplayEnglishAfterKoreanDelete = true;
        }
        consumeClearedPreeditDelete = hasRecentClearedKoreanPreedit(nowMs);
        lastCommittedWasKorean = false;
    }
    public synchronized void keyboardSwitchedToKorean(long ms) {
        koreanTransition = true;
    }
    public synchronized boolean hasRecentClearedKoreanPreedit(long ms) {
        return clearedKoreanPreedit && clearedKoreanPreeditAt >= 0 &&
                ms >= clearedKoreanPreeditAt &&
                ms - clearedKoreanPreeditAt <= EMPTY_KOREAN_DELETE_MS;
    }
    private String removeEnglishEcho(String text, long ms) {
        if (!korean(text) || recentAsciiAt < 0 || ms < recentAsciiAt ||
                ms - recentAsciiAt > ENGLISH_CONTEXT_MS) return text;
        // Android IME may retransmit an already-committed English run as
        // surrounding editor context. Remove the longest *exact* suffix of
        // text we actually delivered, but only if followed by Hangul.
        int limit = Math.min(text.length(), recentAscii.length());
        for (int n = limit; n >= 2; n--) {
            if ((recentAscii.endsWith(text.substring(0, n)) ||
                    recentAscii.startsWith(text.substring(0, n))) &&
                    korean(text.substring(n))) {
                echoSuppressions++;
                if (isPostKoreanDeletion(ms)) lastStaleReplayAt = ms;
                // Remember EXACTLY the context that came with the current
                // Korean composition; it is not a new X11 text insertion.
                mixedContextPrefix = text.substring(0, n);
                drainingMixedContext = false;
                return text.substring(n);
            }
        }
        return text;
    }
    public synchronized void composing(String text, long ms, Sink sink) {
        if (text == null) text = "";
        if (korean(text)) {
            koreanTransition = true;
            lastKoreanActivityAt = ms;
        }
        String normalized = removeEnglishEcho(text, ms);
        // A genuinely separate Korean preedit does not carry the previous
        // Latin editor context. End any stale tracking on that transition.
        if (korean(text) && normalized.equals(text)) {
            mixedContextPrefix = "";
            drainingMixedContext = false;
            drainedMixedContextCommit = "";
        }
        if (normalized.isEmpty() && korean(draft)) {
            clearedKoreanPreeditAt = ms;
            clearedKoreanPreedit = true;
        } else if (!normalized.isEmpty()) {
            clearedKoreanPreedit = false;
            consumeClearedPreeditDelete = false;
        }
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

    /**
     * Call BEFORE the bridge's ordinary ASCII composing path. This is the
     * missing Korean->ASCII transition from the S26U P19 trace, in which
     * setComposingText("old-English-prefix") follows mixed Hangul preedit
     * without any Android DEL at all. Clear only the Korean Fcitx preedit;
     * the old English has already been rendered by X11/XTEST.
     */
    public synchronized boolean consumeRestoredAsciiContext(
            String text, long ms, Sink sink) {
        if (text == null || !ascii(text)) return false;
        if (mixedContextPrefix.isEmpty()) {
            if (!text.isEmpty()) drainedMixedContextCommit = "";
            return false;
        }
        if (!mixedContextPrefix.startsWith(text)) {
            mixedContextPrefix = "";
            drainingMixedContext = false;
            drainedMixedContextCommit = "";
            return false; // a genuine, different ASCII composition
        }
        drainingMixedContext = true;
        echoSuppressions++;
        // A C("") update removes a native preedit without committing it.
        // P19 mistakenly did finished() -> T("한") followed by C(old ASCII)
        // here, so both the Hangul and the English were reinserted.
        if (!draft.isEmpty()) composing("", ms, sink);
        if (text.isEmpty()) {
            // Once Samsung has finished shrinking its stale old-English
            // composition all the way to C(""), the NEXT user-typed ASCII
            // must work normally (even if it starts with the same letter).
            // Keep only a one-shot guard for a possible late stale T.
            drainedMixedContextCommit = mixedContextPrefix;
            mixedContextPrefix = "";
            drainingMixedContext = false;
        }
        return true;
    }

    /** Ignore a finishing T of the exact context drained above. */
    public synchronized boolean consumeRestoredAsciiCommit(String text) {
        if (text == null || text.isEmpty() || !ascii(text)) return false;
        String old = drainingMixedContext ? mixedContextPrefix
                : drainedMixedContextCommit;
        if (old.isEmpty() || !old.startsWith(text)) {
            drainedMixedContextCommit = "";
            return false;
        }
        drainedMixedContextCommit = "";
        echoSuppressions++;
        return true;
    }
    public synchronized void committed(String text, long ms, Sink sink) {
        if (text == null) text = "";
        if (text.length() >= 2 && ascii(text) &&
                isPostKoreanDeletion(ms) && matchesPriorAscii(text)) {
            echoSuppressions++;
            lastStaleReplayAt = ms;
            // This is the final commit of an already ignored English echo,
            // not a new keystroke. Never duplicate the old English text.
            return;
        }
        if (korean(text)) {
            koreanTransition = true;
            lastKoreanActivityAt = ms;
        }
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
            if (korean(normalized)) lastCommittedWasKorean = true;
            else if (!normalized.isEmpty()) {
                lastCommittedWasKorean = false;
                mayReplayEnglishAfterKoreanDelete = false;
                lastStaleReplayAt = -1;
            }
        }
        draft = "";
        koreanTransition = false;
        finishEchoPending = false;
        clearedKoreanPreedit = false;
        consumeClearedPreeditDelete = false;
    }
    public synchronized void finished(long ms, Sink sink) {
        if (!draft.isEmpty()) {
            emittedCommits++;
            sink.committed(draft);
            if (korean(draft)) lastCommittedWasKorean = true;
            lastFinished = draft;
            lastFinishedMs = ms;
            finishEchoPending = true;
        }
        draft = "";
        koreanTransition = false;
        clearedKoreanPreedit = false;
        consumeClearedPreeditDelete = false;
    }
    public synchronized boolean delete(int before, int after, Sink sink) {
        int b = Math.max(0, Math.min(before, 128));
        int a = Math.max(0, Math.min(after, 128));
        if (consumeClearedPreeditDelete && b > 0) {
            // The already-cleared composing glyph accounts for one DEL.
            // Do not forward that DEL to previously committed English.
            b--;
            consumeClearedPreeditDelete = false;
            clearedKoreanPreedit = false;
        }
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
        clearedKoreanPreedit = false;
        consumeClearedPreeditDelete = false;
        withheldAscii = "";
        mixedContextPrefix = "";
        drainingMixedContext = false;
        drainedMixedContextCommit = "";
        // Reinitializing IME's composing state is not an editor focus
        // change. Retain English context and last committed script until
        // clearEditorContext() is explicitly requested.
    }
    public synchronized void clearEditorContext() {
        reset();
        recentAscii = "";
        recentAsciiAt = -1;
        lastFinished = "";
        lastFinishedMs = -1;
        lastCommittedWasKorean = false;
        lastKoreanDeleteAt = -1;
        lastStaleReplayAt = -1;
        mayReplayEnglishAfterKoreanDelete = false;
        lastKoreanActivityAt = -1;
        clearedKoreanPreeditAt = -1;
    }
}
