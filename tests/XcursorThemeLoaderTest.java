package com.alroot.wayland;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public final class XcursorThemeLoaderTest {
    private static void eq(boolean yes, String message) {
        if (!yes) throw new AssertionError(message);
    }

    private static void little(DataOutputStream out, int n) throws Exception {
        out.writeInt(Integer.reverseBytes(n));
    }

    private static byte[] chunk(int size, int width, int height, int hx, int hy) throws Exception {
        ByteArrayOutputStream memory = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(memory);
        for (int n : new int[] {36, 0xfffd0002, size, 1, width, height, hx, hy, 50})
            little(out, n);
        for (int i = 0; i < width * height; i++) little(out, 0xffffffff);
        return memory.toByteArray();
    }

    private static void syntheticTest() throws Exception {
        Path root = Files.createTempDirectory("alroot-s22-cursor-");
        try {
            Path cfg = root.resolve(".config/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml");
            Files.createDirectories(cfg.getParent());
            Files.write(cfg, ("<channel><property name=\"Gtk\"><property " +
                    "name=\"CursorThemeSize\" type=\"int\" value=\"24\"/>" +
                    "<property name=\"CursorThemeName\" type=\"string\" " +
                    "value=\"TestTheme\"/></property></channel>")
                    .getBytes(StandardCharsets.UTF_8));
            Path cursor = root.resolve(".icons/TestTheme/cursors/default");
            Files.createDirectories(cursor.getParent());
            byte[] small = chunk(16, 8, 8, 0, 0);
            byte[] correct = chunk(24, 32, 32, 4, 4);
            ByteArrayOutputStream memory = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(memory);
            for (int n : new int[] {0x72756358, 16, 0x00010000, 2,
                    0xfffd0002, 16, 40, 0xfffd0002, 24, 40 + small.length}) little(out, n);
            out.write(small);
            out.write(correct);
            Files.write(cursor, memory.toByteArray());
            XcursorThemeLoader.Frame f = XcursorThemeLoader.fromXfceSettings(
                    root.toFile(), root.resolve("prefix").toFile());
            eq(f != null, "theme cursor preloaded");
            eq("TestTheme".equals(f.theme), "theme from XFCE, not hard-coded");
            eq(f.width == 32 && f.height == 32, "selected nominal size 24");
            eq(f.hotspotX == 4 && f.hotspotY == 4, "hotspot preserved");
            eq(f.pixels.length == 1024 && f.pixels[0] == 0xffffffff, "actual ARGB decoded");

            // Invalid/traversal theme names cannot access arbitrary paths.
            Files.write(cfg, ("<channel><property name=\"CursorThemeName\" " +
                    "value=\"../TestTheme\"/></channel>").getBytes(StandardCharsets.UTF_8));
            eq(XcursorThemeLoader.fromXfceSettings(root.toFile(), root.toFile()) == null,
                    "theme path traversal rejected");
            Files.write(cfg, ("<channel><property name=\"CursorThemeName\" " +
                    "value=\"TestTheme\"/></channel>").getBytes(StandardCharsets.UTF_8));
            Files.write(cursor, new byte[] {0, 0, 0, 0});
            eq(XcursorThemeLoader.fromXfceSettings(root.toFile(), root.toFile()) == null,
                    "malformed cursor rejected");
            System.out.println("S22_XCURSOR_THEME_SELECTION_HOTSPOT_BOUNDS=PASS");
        } finally {
            // Owned temporary test directory only, never XFCE user settings.
            try (java.util.stream.Stream<Path> paths = Files.walk(root)) {
                paths.sorted(java.util.Comparator.reverseOrder()).forEach(path -> {
                    try { Files.delete(path); } catch (Exception e) { throw new RuntimeException(e); }
                });
            }
        }
    }

    public static void main(String[] args) throws Exception {
        syntheticTest();
        File home = new File("/data/data/com.termux/files/home");
        if (home.isDirectory()) {
            XcursorThemeLoader.Frame frame = XcursorThemeLoader.fromXfceSettings(
                    home, new File("/data/data/com.termux/files/usr"));
            eq(frame != null, "S26U configured XFCE cursor image exists");
            eq(frame.width > 0 && frame.height > 0 && frame.hotspotX < frame.width,
                    "real theme frame bounded");
            System.out.println("S22_S26U_XFCE_CURSOR=" + frame.theme + " " +
                    frame.width + "x" + frame.height + " hotspot=" +
                    frame.hotspotX + "," + frame.hotspotY + " PASS");
        }
    }
}
