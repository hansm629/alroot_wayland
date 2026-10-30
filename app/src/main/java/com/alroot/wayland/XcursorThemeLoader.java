package com.alroot.wayland;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Reads XFCE's existing Xcursor default image for the *startup-only* pointer.
 * No GTK/Wayland initialization, driver changes or external image converter.
 * Xcursor images contain premultiplied ARGB pixels and an explicit hotspot.
 */
final class XcursorThemeLoader {
    private static final int XCURSOR_MAGIC = 0x72756358; // "Xcur", little endian
    private static final int IMAGE_TYPE = 0xfffd0002;
    private static final long MAX_FILE_SIZE = 8L * 1024 * 1024;
    private static final Pattern ATTRIBUTE = Pattern.compile(
            "\\b(name|value)\\s*=\\s*[\"']([^\"']*)[\"']");
    private static final Pattern PROPERTY = Pattern.compile("<property\\b[^>]{0,1024}>");

    static final class Frame {
        final String theme;
        final int width, height, hotspotX, hotspotY;
        final int[] pixels;

        Frame(String theme, int w, int h, int x, int y, int[] rgba) {
            this.theme = theme;
            width = w;
            height = h;
            hotspotX = x;
            hotspotY = y;
            pixels = rgba;
        }
    }

    private XcursorThemeLoader() {}

    static Frame fromXfceSettings(File home, File prefix) {
        String rel = "xfce4/xfconf/xfce-perchannel-xml/xsettings.xml";
        File privateConfig = new File(home, ".alroot-wayland/xfce-config/" + rel);
        File original = new File(home, ".config/" + rel);
        // Alroot mirrors the original xsettings.xml each time XFCE starts.
        // Prefer that source even before the new session has begun mirroring.
        File config = original.isFile() ? original : privateConfig;
        String theme = property(config, "CursorThemeName");
        int size = 24;
        try {
            String setting = property(config, "CursorThemeSize");
            if (setting != null) size = Integer.parseInt(setting);
        } catch (NumberFormatException ignored) { }
        if (size < 8 || size > 256) size = 24;
        if (!validName(theme)) return null;
        return findTheme(home, prefix, theme, size, new HashSet<>(), 0);
    }

    private static String property(File config, String name) {
        try {
            if (!config.isFile() || config.length() > 65536) return null;
            String text = new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8);
            Matcher properties = PROPERTY.matcher(text);
            while (properties.find()) {
                String attrName = null, attrValue = null;
                Matcher attrs = ATTRIBUTE.matcher(properties.group());
                while (attrs.find()) {
                    if ("name".equals(attrs.group(1))) attrName = attrs.group(2);
                    if ("value".equals(attrs.group(1))) attrValue = attrs.group(2);
                }
                if (name.equals(attrName)) return attrValue;
            }
        } catch (IOException | SecurityException ignored) { }
        return null;
    }

    private static boolean validName(String theme) {
        return theme != null && theme.matches("[a-zA-Z0-9_+.-]{1,128}") &&
                !theme.equals(".") && !theme.equals("..");
    }

    private static Frame findTheme(File home, File prefix, String theme, int size,
            Set<String> seen, int depth) {
        if (depth > 5 || !validName(theme) || !seen.add(theme)) return null;
        File[] roots = {
                new File(home, ".icons"), new File(home, ".local/share/icons"),
                new File(prefix, "share/icons")
        };
        for (File root : roots) {
            File dir = new File(root, theme);
            for (String cursor : new String[] {"left_ptr", "default"}) {
                Frame image = readCursor(new File(dir, "cursors/" + cursor), theme, size);
                if (image != null) return image;
            }
        }
        // Xcursor themes can inherit an actual cursor directory from a parent.
        for (File root : roots) {
            File index = new File(new File(root, theme), "index.theme");
            try {
                if (!index.isFile() || index.length() > 65536) continue;
                for (String line : Files.readAllLines(index.toPath(), StandardCharsets.UTF_8)) {
                    if (!line.trim().startsWith("Inherits=")) continue;
                    for (String parent : line.substring(line.indexOf('=') + 1).split(",")) {
                        Frame found = findTheme(home, prefix, parent.trim(), size, seen, depth + 1);
                        if (found != null) return found;
                    }
                }
            } catch (IOException | SecurityException ignored) { }
        }
        return null;
    }

    static Frame readCursor(File file, String theme, int targetSize) {
        try (RandomAccessFile f = new RandomAccessFile(file, "r")) {
            long length = f.length();
            if (length < 28 || length > MAX_FILE_SIZE || little(f) != XCURSOR_MAGIC) return null;
            long header = uint(f), version = uint(f), count = uint(f);
            if (header < 16 || header > length - 12 || version != 0x00010000 ||
                    count < 1 || count > 2048 || header + 12 * count > length) return null;

            long bestOffset = -1, bestDistance = Long.MAX_VALUE;
            f.seek(header);
            for (int i = 0; i < count; ++i) {
                long type = uint(f), subtype = uint(f), offset = uint(f);
                long distance = Math.abs(subtype - (long)targetSize);
                if (type == (IMAGE_TYPE & 0xffffffffL) && offset <= length - 36 &&
                        distance < bestDistance) {
                    bestDistance = distance;
                    bestOffset = offset;
                }
            }
            if (bestOffset < 0) return null;
            f.seek(bestOffset);
            long chunkHeader = uint(f), type = uint(f);
            uint(f); // nominal image size
            uint(f); // image version
            long width = uint(f), height = uint(f), hx = uint(f), hy = uint(f);
            uint(f); // frame delay (first frame is sufficient for an arrow)
            if (chunkHeader < 36 || type != (IMAGE_TYPE & 0xffffffffL) ||
                    width < 1 || width > 256 || height < 1 || height > 256 ||
                    hx >= width || hy >= height ||
                    bestOffset + chunkHeader + width * height * 4 > length) return null;
            f.seek(bestOffset + chunkHeader);
            int[] pixels = new int[(int)(width * height)];
            for (int i = 0; i < pixels.length; ++i) {
                int argb = little(f);
                int alpha = argb >>> 24;
                if (alpha != 0 && alpha != 255) {
                    int red = Math.min(255, ((argb >>> 16) & 255) * 255 / alpha);
                    int green = Math.min(255, ((argb >>> 8) & 255) * 255 / alpha);
                    int blue = Math.min(255, (argb & 255) * 255 / alpha);
                    argb = (alpha << 24) | (red << 16) | (green << 8) | blue;
                }
                pixels[i] = argb;
            }
            return new Frame(theme, (int)width, (int)height, (int)hx, (int)hy, pixels);
        } catch (IOException | SecurityException ignored) {
            return null; // preserve the previous small built-in arrow fallback
        }
    }

    private static int little(RandomAccessFile f) throws IOException {
        return Integer.reverseBytes(f.readInt());
    }

    private static long uint(RandomAccessFile f) throws IOException {
        return Integer.toUnsignedLong(little(f));
    }
}
