# GPU-S22 — Start XFCE with its configured cursor, no generic interim arrow

Baseline: S21 (`960fb06`). No changes to the native input pipeline, IME,
GPU buffers, X11 WSI, desktop theme, global Termux-X11, or stored XFCE settings.

## Root cause

The S21 Android cursor overlay draws a generic fallback arrow until the
Wayland client publishes a cursor image. In the S26U's Xfce configuration,
`Gtk/CursorThemeName=Layan-white-cursors`, `Gtk/CursorThemeSize=24`, but
private Xwayland did not explicitly inherit those Xcursor environment values.
The user can therefore see a generic pointer first and the XFCE cursor later.

## Scoped fix

- `XcursorThemeLoader` reads the current user xsettings.xml without modifying
  it. It uses the installed Xcursor theme's `left_ptr`/`default` pixel data
  and hotspot, not a hard-coded raster or Android's system pointer.
- `CursorOverlay` draws that themed image at the existing centered pointer
  location while the native cursor is unavailable. When Wayland publishes its
  cursor, it remains authoritative for normal per-window cursor changes.
- `termux/xfce-cursor-env.py` reads the same XFCE setting and the isolated
  session startup exports `XCURSOR_THEME` and `XCURSOR_SIZE` **before** starting
  nested Labwc or private rootful Xwayland. The inherited environment stays
  within the Alroot session; Termux-X11 `:0` is untouched.
- Invalid, missing, oversized or unsupported Xcursor assets fall back to the
  previous small S21 arrow. No forced cursor warps, no duplicate pointers.

The S26U's Xcursor default is nominal size 24, actual 32x32 ARGB with hotspot
(4,4). These dimensions are read from disk rather than assumed by the app.

## Tests

Run `bash graphics/tools/test-s22-xfce-startup-cursor.sh` in Termux.
It checks the real theme, synthetic theme changes, image size selection,
hotspot, corrupt input, shell syntax, and existing S20/S21 pointer/PiP tests.

After the signed APK is installed and the session is deliberately restarted,
verify visually: center cursor is already in the current XFCE theme on the
first frame; no temporary generic arrow, no size/hotspot shift; moving over
a text field still changes the cursor as appropriate; touchpad, external
mouse and PiP restore remain normal. Do not call visual checks PASS until the
user tests them on the device.
