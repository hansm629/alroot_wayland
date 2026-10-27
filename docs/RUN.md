# Alroot Wayland P01 — awstart 실행 및 테스트

This document is for the project-only Alroot Wayland helper, NOT tx11start.
The normal path: Android helper → nested Labwc/pixman → separate rootful
Xwayland → XFCE4/xfwm4. Direct SHM is a diagnostic-only profile.

## 0. Install or update the APK

All commands below run in the NATIVE Termux shell, not Debian/PRoot.

    cd ~/alroot_wayland
    bash tools/install-cli.sh
    awstart --doctor
    awstart --smoke

The helper must be signed with the installed Termux certificate. A newly
built APK that includes LaunchInbox is required for the default --topapp
launcher. Android installation still requires approval:

    awstart --install-apk

Android installation is asynchronous. An installer-requested message does
not prove completion. Run awstart --status to query Android PackageManager.
When the installed APK is byte-identical to the locally signed artifact,
status reports INSTALLED (matches latest locally signed APK).
The installer action will not launch again for the same already-installed APK.
If installation is still pending, approve it on Android before starting.

On 2026-10-27, the S26U's installed base.apk was byte-identical to the local
signed P01 APK. Running the default awstart --debug was also verified:
the foreground Activity accepted the launch, with the Alroot host READY and
Labwc, rootful Xwayland, XFCE and clipboard processes RUNNING on isolated :90.
Actual on-screen input/IME acceptance remains separately TEST-PENDING.

Before upgrading an already active APK, stop this Alroot session and native
server through its app menu. Never uninstall Termux or Termux-X11.

## 1. One-command startup

    awstart

Default is xfce-labwc. The APK Activity is brought to the foreground and
consumes a private, single-use, fixed-profile request. No arbitrary Android
broadcast command is used. A request acknowledgment is NOT desktop readiness.

The guest compositor uses pixman/SHM; Android's native host still uses Vulkan.
Begin with Android app output size set to 1280x720 so it matches the P01
rootful Xwayland baseline. Output-size synchronization is part of P02.

Older APK fallback (Termux-shell spawn, NOT top-app performance verified):

    awstart --no-topapp

If the same profile is already running, awstart only reopens the Activity.
A restart that ends the existing Alroot session is always explicit.

## 2. tx11start-style command options

    awstart --help
    awstart --status
    awstart --logs
    awstart --doctor
    awstart --open
    awstart --stop
    awstart --restart
    awstart --direct
    awstart --no-topapp --direct
    awstart --debug
    awstart --dry-run

--direct selects xfce-shm-direct: host → rootful Xwayland → XFCE4. It does
NOT mean the normal nested Labwc profile works.

--stop only signals project-recorded session PIDs, not Termux-X11 :0 and
not the Android native host. Stop the native host explicitly in the APK menu.

Unlike tx11start, awstart does not kill old desktops, set global Mesa
variables, change GPU clocks, run a GPU keep-alive, or reset PulseAudio.
Resolution, orientation, fullscreen, cursor capture and Android IME remain
in the APK settings UI.

## 3. Live XFCE verification

    awstart --status
    awstart --logs

Expected: host READY, labwc RUNNING, xwayland RUNNING, xfce RUNNING,
clipboard RUNNING. The normal nested Labwc creates its own Wayland socket
(e.g. wayland-1); rootful Xwayland uses a separate free X DISPLAY (e.g. :90).
NEVER assume :0.

Read the actual project display, not the inherited Termux-X11 display:

    display="$(cat ~/.alroot-wayland/display)"
    DISPLAY="$display" XAUTHORITY="$HOME/.alroot-wayland/Xauthority" xdpyinfo | grep -E 'name of display|dimensions'
    DISPLAY="$display" XAUTHORITY="$HOME/.alroot-wayland/Xauthority" xrandr --query | head -8

In the APK visually confirm the XFCE panel, terminal and Thunar. Test
mouse movement/buttons, scroll, drag-select, manual desktop capture,
Ctrl+Alt+Esc release, Android Back keyboard toggle, shortcuts, Korean
committed text/preedit, deletion, and bidirectional multiline clipboard.

Then switch apps or rotate and reopen. The Wayland native host and guest
session should remain alive. Actual screen/input tests are still required.

## 4. Failure logs and safe recovery

    awstart --logs
    logcat -d -s AlrootWayland DroidDeckWayland | tail -60

If normal Labwc fails, stop this session, then try the direct diagnostic:

    awstart --stop
    awstart --direct

The project-private wlroots path fixes bionic SHM and Termux's X11 socket
directory; it never replaces system wlroots/Mesa. Rebuild it if missing:

    bash ~/alroot_wayland/packages/build-wlroots-termux.sh

Stop project-owned desktop processes only:

    awstart --stop

The Android host stays active until stopped using its app menu. The user's
original Termux-X11/tx11start remains untouched.

## Acceptance boundary

GitHub APK BUILD PASS, a matching signing certificate, and live process
status do NOT prove on-screen frame correctness, IME or input PASS.
Default awstart --topapp causes Activity-side spawn, but only actual child
thread cgroup/cpuset measurements can establish performance policy in P02.

## Black-screen correction and actual-pixel diagnostics

Previous process-only success was incorrectly reported as successful desktop
output. XFCE4 GTK components had auto-selected Wayland; xfwm4 was missing
from the isolated project Xwayland. The launcher now forces X11 backends only
for the project XFCE children.

    awstart --stop
    awstart --debug
    awstart --status

The new x11_desktop: READY indicator requires xfwm4, xfdesktop and panel
registered in the actual isolated X DISPLAY. This is not independently proof
of correct pixels on Android.

APK v0.1.2 samples only its own Android SurfaceView through PixelCopy at
160x90 every 12 seconds while in foreground (first sample after 8 seconds).
After user-approved APK update and opening the helper, check:

    logcat -d -s AlrootWayland | grep PixelProbe | tail -6

Nonblack sample counts mean the actual Android SurfaceView contains color;
zero nonblack or a PixelCopy error narrows investigation to the rendering or
presentation layers beyond Xwayland. No raw screenshot is stored. The user
still needs to visually verify desktop content and input.
