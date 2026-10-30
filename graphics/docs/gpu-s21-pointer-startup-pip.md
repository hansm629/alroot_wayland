# GPU-S21 — XFCE startup pointer and Android PiP cursor restoration

## Scope

- Source baseline: `b8dcd13` S20 atomic scene click, left unchanged.
- The Android touchpad view now owns **one** visible software cursor from startup,
  hiding the unrelated hardware cursor. If the native Labwc bitmap has not
  arrived, a small density-independent fallback arrow appears at the center.
- Physical mouse motion still transfers cursor ownership to Android's hardware
  pointer. Captured/game mode still sends relative input.
- When the Android View enters PiP or returns to full-screen, existing pointer
  coordinates are remapped across old/new viewports instead of clamped in the
  shrunken PiP bounds. Resizing is **not** a guest pointer movement.
- When PiP status changes, invalidate the overlay even if the native guest
  cursor bitmap serial has not changed.
- The S20 atomic mouse button packet, native pointer pipeline, IME/Fcitx,
  Termux-X11 DRI3 and experimental GPU profiles were **not** changed.

## Verification

Run from the S26U Termux shell:

```sh
cd ~/alroot_wayland
bash graphics/tools/test-s21-pointer-startup-pip.sh
```

This includes the original S20 atomic-pointer and P18 startup tests, plus
repeated PiP/fullscreen geometry round trips. They cannot prove actual Android
pointer visibility. After a newly signed APK is installed, verify on S26U:

1. Start `awstart` with the existing session profile. Without touching the
   display, verify **one** cursor at the desktop center (not top-left).
2. Move via touchpad and right-click an XFCE terminal; cursor and hit-test
   must agree, without duplicated hardware/software pointers.
3. Press Android Home to enter PiP; return to fullscreen without touching.
   Cursor should reappear at the **same logical desktop position**, rather
   than near the top-left. Repeat after moving near a screen edge.
4. Test both physical mouse/DeX and pointer-captured modes; the project must
   not introduce two cursors or change relative input behavior.
5. Confirm one short native Vulkan/GLX application still renders in XFCE.

Actual display, PiP, external-device and IME checks remain TEST-PENDING until
visually exercised by the user. The outstanding wrapper+Zink and Turnip+Zink
Xwayland acceleration agenda stays separate from this input-only fix.
