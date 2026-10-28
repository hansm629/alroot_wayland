import com.alroot.wayland.ImeCommitState;
import com.alroot.wayland.ImeCursorTracker;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;

/** Synthetic Samsung sequence: English → Korean → DEL → stale English echo.
 * Connects to the REAL project-native IME server on a disposable Xvfb only.
 */
public final class ImeEnglishEchoNativeProbe {
    private static final class NativeSink implements ImeCommitState.Sink {
        private final SocketChannel socket;
        NativeSink(SocketChannel socket) { this.socket = socket; }
        void send(char op, String text) {
            try {
                byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
                ByteBuffer frame = ByteBuffer.allocate(bytes.length + 5);
                frame.order(ByteOrder.BIG_ENDIAN);
                frame.put((byte) op).putInt(bytes.length).put(bytes).flip();
                while (frame.hasRemaining()) socket.write(frame);
            } catch (Exception e) { throw new IllegalStateException(e); }
        }
        @Override public void composing(String text) { send('C', text); }
        @Override public void committed(String text) { send('T', text); }
        @Override public void delete(int before, int after) {
            send('D', before + " " + after);
        }
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1 && args.length != 2)
            throw new IllegalArgumentException("socket [p16]");
        try (SocketChannel socket = SocketChannel.open(StandardProtocolFamily.UNIX)) {
            socket.connect(UnixDomainSocketAddress.of(args[0]));
            ImeCommitState state = new ImeCommitState();
            ImeCursorTracker fake = new ImeCursorTracker();
            NativeSink x = new NativeSink(socket);
            if (args.length == 2 && "p16".equals(args[1])) {
                state.committed("hello", 100, x);
                Thread.sleep(110);
                state.composing("hel한", 5000, x); // old English prefix + Hangul
                Thread.sleep(95);
                state.composing("", 5010, x); // Samsung clears the native preedit first
                Thread.sleep(45);
                state.backspace(5050);
                state.delete(1, 0, x); // must not delete 'o' in prior hello
                for (String prefix : new String[]{"h", "he", "hel", "hell", "hello"}) {
                    if (!state.withholdSuspectedAscii(prefix, 5060))
                        throw new AssertionError("P16 old English replay prefix was not withheld");
                }
                if (!state.suppressWithheldAsciiCommit("hello", 5070))
                    throw new AssertionError("P16 old ASCII commit was not suppressed");
                Thread.sleep(135);
                System.out.println("P16_PREFIX_REPLAY_FILTERED=1 "
                        + "cleared_korean_preedit=1 old_english_remains_once");
                return;
            }
            state.committed("hello", 100, x); // existing English committed
            fake.begin();
            fake.committed(5, 1);
            fake.end();
            Thread.sleep(100);
            state.composing("한", 5000, x);
            Thread.sleep(80);
            state.committed("한", 5100, x);
            Thread.sleep(80);
            // A Samsung/Gboard editor occasionally moves its phantom
            // selection beyond the synthetic context and then deletes the
            // *fake whitespace*. This MUST NOT delete a real X11 character.
            fake.begin();
            fake.selected(10, 10);
            if (!fake.phantomDelete(3, 0) || !fake.phantomDelete(1, 0) ||
                    fake.phantomDelete(1, 0)) {
                throw new AssertionError("IME phantom-space cleanup misclassified");
            }
            fake.end();
            System.out.println("P13_PHANTOM_DELETE_IGNORED=" +
                    fake.ignoredPhantomDeletes());
            state.backspace(5105);            // same path as both Android DEL APIs
            state.delete(1, 0, x);
            Thread.sleep(80);
            boolean replay = state.duplicateAsciiComposing("hello", 5115);
            state.committed("hello", 5120, x); // Samsung stale English commit
            Thread.sleep(120);
            if (!replay || state.suppressedEchoCount() < 2)
                throw new AssertionError("Old English was not filtered after Korean DEL");
            System.out.println("P12_ENGLISH_ECHO_FILTERED=1 suppressions=" +
                    state.suppressedEchoCount() + " no-old-English-reinsert");
        }
    }
}
