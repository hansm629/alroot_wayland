import com.alroot.wayland.ImeCommitState;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;

/** Disposable JVM -> actual native UNIX socket -> Xvfb GTK end-to-end probe. */
public final class ImeRealtimeBridgeProbe {
    static final class BridgeSink implements ImeCommitState.Sink {
        final SocketChannel channel;
        BridgeSink(SocketChannel ch) { channel = ch; }
        void packet(char op, String value) {
            try {
                byte[] data = value.getBytes(StandardCharsets.UTF_8);
                ByteBuffer packet = ByteBuffer.allocate(5 + data.length);
                packet.order(ByteOrder.BIG_ENDIAN);
                packet.put((byte)op).putInt(data.length).put(data).flip();
                while (packet.hasRemaining()) channel.write(packet);
            } catch (Exception e) {
                throw new IllegalStateException("X11 test daemon disconnected", e);
            }
        }
        @Override public void composing(String value) { packet('C', value); }
        @Override public void committed(String value) { packet('T', value); }
        @Override public void delete(int before, int after) {
            packet('D', before + " " + after);
        }
    }
    private static void shortPause() throws InterruptedException {
        Thread.sleep(22);
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("socket path");
        try (SocketChannel channel = SocketChannel.open(StandardProtocolFamily.UNIX)) {
            channel.connect(UnixDomainSocketAddress.of(args[0]));
            BridgeSink x = new BridgeSink(channel);
            ImeCommitState state = new ImeCommitState();
            long t = 100;
            // Mimic existing immediate English C/T before switching to Hangul.
            x.packet('C', "hello");
            x.packet('T', "hello");
            state.alreadyInsertedAscii("hello", t);
            Thread.sleep(110);

            long firstNs = System.nanoTime();
            state.composing("helloㅎ", t += 40, x);
            System.out.println("FIRST_HANGUL_PREEDIT_SEND_US=" + firstNs / 1000);
            shortPause();
            state.composing("hello하", t += 40, x);
            shortPause();
            state.composing("hello한", t += 40, x);
            shortPause();
            state.committed("hello한", t += 40, x);
            shortPause();
            // Korean deletion must be immediately emitted via native D.
            state.delete(1, 0, x);
            shortPause();
            state.composing("ㄱ", t += 40, x);
            shortPause();
            state.composing("구", t += 40, x);
            shortPause();
            state.composing("국", t += 40, x);
            shortPause();
            state.committed("국", t += 40, x);
            Thread.sleep(180);
            System.out.println("REALTIME_STREAM_PREEDIT_COUNT=" + state.composingCount());
            System.out.println("REALTIME_STREAM_COMMIT_COUNT=" + state.committedCount());
        }
    }
}
