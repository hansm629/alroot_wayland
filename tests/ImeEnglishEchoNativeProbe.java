import com.alroot.wayland.ImeCommitState;
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
        if (args.length != 1) throw new IllegalArgumentException("socket");
        try (SocketChannel socket = SocketChannel.open(StandardProtocolFamily.UNIX)) {
            socket.connect(UnixDomainSocketAddress.of(args[0]));
            ImeCommitState state = new ImeCommitState();
            NativeSink x = new NativeSink(socket);
            state.committed("hello", 100, x); // existing English committed
            Thread.sleep(100);
            state.composing("한", 5000, x);
            Thread.sleep(80);
            state.committed("한", 5100, x);
            Thread.sleep(80);
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
