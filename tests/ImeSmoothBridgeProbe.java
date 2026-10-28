import com.alroot.wayland.ImeCommitState;
import com.alroot.wayland.ImeCompositionSmoother;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;

/**
 * Reproduces the exact P10 composition/edit output ordering with the native
 * Unix socket and an isolated real GTK entry. No real user's X11 displays.
 */
public final class ImeSmoothBridgeProbe {
    static final class BridgeSink implements ImeCommitState.Sink {
        final SocketChannel ch;
        final ImeCompositionSmoother smoother = new ImeCompositionSmoother();
        int sentComposing, sentEmpty, sentCommitted, preventedEmpty;
        BridgeSink(SocketChannel channel) { ch = channel; }

        void packet(char op, String value) {
            try {
                byte[] payload = value.getBytes(StandardCharsets.UTF_8);
                ByteBuffer frame = ByteBuffer.allocate(5 + payload.length)
                        .order(ByteOrder.BIG_ENDIAN);
                frame.put((byte)op).putInt(payload.length).put(payload).flip();
                while (frame.hasRemaining()) ch.write(frame);
                if (op == 'C') {
                    sentComposing++;
                    if (value.isEmpty()) sentEmpty++;
                } else if (op == 'T') sentCommitted++;
            } catch (Exception e) {
                throw new IllegalStateException("Native bridge write failed", e);
            }
        }
        @Override public void composing(String text) {
            if (text.isEmpty()) {
                smoother.deletedRevision("", value -> packet('C', value));
                return;
            }
            if (smoother.hasPending()) preventedEmpty++;
            smoother.replaced(text, value -> packet('C', value));
        }
        @Override public void committed(String text) {
            smoother.cancel();
            packet('T', text);
        }
        @Override public void delete(int before, int after) {
            smoother.flush(value -> packet('C', value));
            packet('D', before + " " + after);
        }
        final ImeCommitState.Sink deletionSink = new ImeCommitState.Sink() {
            @Override public void composing(String text) {
                smoother.deletedRevision(text, value -> packet('C', value));
            }
            @Override public void committed(String text) {
                BridgeSink.this.committed(text);
            }
            @Override public void delete(int before, int after) {
                BridgeSink.this.delete(before, after);
            }
        };
    }
    static void check(boolean valid, String desc) {
        if (!valid) throw new AssertionError(desc);
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 2)
            throw new IllegalArgumentException("socket path and scenario");
        final String scenario = args[1];
        if (!scenario.equals("delete") && !scenario.equals("empty-preedit"))
            throw new IllegalArgumentException("unsupported scenario");
        try (SocketChannel ch = SocketChannel.open(StandardProtocolFamily.UNIX)) {
            ch.connect(UnixDomainSocketAddress.of(args[0]));
            BridgeSink bridge = new BridgeSink(ch);
            ImeCommitState state = new ImeCommitState();
            state.composing("한", 100, bridge);
            Thread.sleep(85); // expose existing composed glyph on GTK screen
            if (scenario.equals("delete")) {
                check(state.delete(1, 0, bridge.deletionSink),
                        "DEL must consume a composing glyph");
            } else {
                // Samsung Keyboard can also clear its preedit with
                // setComposingText("") without a separate DEL API call.
                state.composing("", 112, bridge);
            }
            check(bridge.smoother.hasPending(),
                    "empty composition erase must be held briefly");
            Thread.sleep(3); // Samsung emits a revised preedit in the same batch
            state.composing("하", 116, bridge);
            check(!bridge.smoother.hasPending(),
                    "new composition must cancel deferred empty erase");
            state.committed("하", 130, bridge);
            Thread.sleep(180);
            System.out.println("P10_JAVA_EMITTED_COMPOSING=" + bridge.sentComposing);
            System.out.println("P10_JAVA_EMPTY_C_FRAMES=" + bridge.sentEmpty);
            System.out.println("P10_JAVA_SUPPRESSED_EMPTY=" + bridge.preventedEmpty);
            System.out.println("P10_JAVA_COMMITS=" + bridge.sentCommitted);
            check(bridge.sentEmpty == 0 && bridge.preventedEmpty == 1,
                    "intermediate empty glyph unexpectedly sent to X11");
        }
    }
}
