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
    logcat -d -s AlrootWayland | tail -60

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

## P02 usability update: fullscreen, pointer, power, dock, overlay

Android fullscreen now requests immersive edge-to-edge layout with system
bars hidden. P02 revision 2 replaces the distorting fullscreen STRETCH
path with aspect-preserving FIT. A 1920x1080 rootful Xwayland (96x96 DPI)
now matches the native Wayland scene resolution. On landscape phones wider
than 16:9, FIT fills the entire available height but necessarily leaves
black margins on left/right; no nonuniform scaling or cropping occurs.
Change the Android guest resolution via the app menu, then run `awstart
--restart` to apply the same new geometry to rootful Xwayland. The optional
windowed mode still offers fit/stretch/fill.

The Alroot menu defaults to a compact 44dp handle in the top-left with its
status text hidden. Tap the handle for settings. Long-press the handle to
temporarily show status. Select Hide Alroot handle to remove it entirely.
Restore via a three-finger tap anywhere on the display, or Ctrl+Alt+M using
a physical keyboard. Ctrl+Alt+Esc still releases pointer capture.

Touch input mode choices are:
1. Touchpad (default) — relative finger movement, tap=left click,
   two-finger tap=right click, two-finger drag=scroll, long-press and
   drag=left-drag.
2. Virtual touchscreen — screen position moves/clicks the mouse directly.
3. Direct touch — native Wayland touch events (Xwayland apps may use
   compositor pointer fallback).

The menu offers pointer speed 50–300 (default 190) and acceleration 0–200
(default 110). Settings persist in the Android app's isolated preferences.

In the project-specific XFCE session, the original battery plugin relies on
unavailable UPower D-Bus. Alroot's GenMon widget now reads a same-UID,
private Android BatteryManager JSON snapshot generated by HelperService
every 60 seconds (widget refresh every 15 seconds). Termux:API is used only
as fallback. The source Plank autostart entry, launchers AND original DConf
database (dock order, icon size, theme and zoom) are copied before the
isolated XFCE session starts. Existing XFCE panel/theme/desktop settings
are refreshed into the isolated copy, and GenMon is added afterward.
The original ~/.config/xfce4, ~/.config/autostart, Termux-X11 and Plank are
untouched. The new package requirement is xfce4-genmon-plugin; no rootfs or
system GPU changes are needed.

Before testing a newly installed APK, stop only the Alroot XFCE session:

    awstart --stop
    awstart --install-apk

Approve Android Update, then reopen and run:

    awstart --debug
    awstart --status
    bash ~/alroot_wayland/tests/p02-ux-smoke.sh

Verify full display coverage, cursor behavior under all three input modes,
battery percentage, dock at screen bottom, status hide/reopen and the
original Termux-X11 session separately. Java compilation and CLI checks
alone do not prove the touch/display output on a physical S26U.

## P02 revision 2: actual X11 / Unicode checks

The old rootful server was fixed at 1280x720 even if the Android guest was
set to 1920x1080. The native guest resolution is now written to
~/.alroot-wayland/output-resolution by the Android helper; rootful Xwayland
reads it at startup, with 1920x1080 as the default and explicit `-dpi 96`.

In the isolated X11 live test, xdpyinfo reported exactly 1920x1080 and
96x96 DPI. The original user Plank DConf and the Alroot copy had identical
SHA256 hashes immediately after the private-profile snapshot.

IME: A disposable X11 XTEST probe found native `xdotool type` produced
keysym=0 for the Korean test. A scoped UTF-8 clipboard paste now handles
non-ASCII text; ASCII remains XTEST. Unicode (2 Korean chars / 6 bytes)
was verified in a disposable GTK 3 text field and a separate Xfce4-terminal
on the project's :90, with the previous clipboard restored byte-identically.
To avoid Android cross-process /proc restrictions, Xwayland now publishes
an ime-active marker when its authenticated X socket is ready. Android
soft-keyboard end-to-end still needs the updated APK and user testing.

The temporary white cross drawn when the Wayland cursor bitmap was
missing has been removed. Android on-screen final aspect correctness and
software IME remain TEST-PENDING until physical device verification.

## P03 fundamentals: real Android refresh, IME, battery visibility, PRoot

The host previously ran Choreographer at roughly 1200 ticks per ten
seconds but continued to advertise 60000 mHz through wl_output. Native
WaylandCompositor already had a JNI method to update the output's refresh
rate. P03 now connects Android Display.getRefreshRate() to that method,
requests the active rate from the Surface (FIXED_SOURCE, seamless where
supported), monitors real Choreographer frame timestamps and updates
wl_output if measured VSync differs. It does not hardcode 120Hz and
does not claim that setting the Xrandr label alone guarantees scanout.

After Android APK update, use:

    awstart --refresh-diag
    logcat -d -s AlrootWayland | grep DisplayTiming | tail -15
    logcat -d -s AlrootWayland | grep -E 'ticks|on screen' | tail -10

The above reports the Android active physical display rate, measured
Android VSync cadence, the advertised Wayland mode and actual host
presentation counters. Xwayland should reflect the active physical rate,
but a slow desktop workload can still present fewer distinct frames
than that rate. On-device validation is required.

For Samsung/Gboard soft input, the Android SurfaceView now uses a
fake-text InputConnection comparable to the Termux-X11 design rather
than pretending the remote X11 editor is a local editable buffer.
Composing text is delivered incrementally (and replaced using X11
Backspace) so clients do not have to wait for nonempty commitText.
Empty commitText terminates composition without erasing prior preedit.
The UTF-8 clipboard-based Unicode bridge and ASCII XTEST fallback
remain project-only. No input text is written into Android logs.

The XFCE GenMon plugin now shows a persistent BAT label even if battery
reading temporarily fails. Android BatteryManager cache supplies the
percentage and remains visible under the project-only panel configuration.
The old UPower-based plugin is excluded from the Alroot panel's plugin ids
without deleting it from original Termux-X11.

For Debian PRoot, normal Termux-X11 mpd/mprun/pdrun still default to
DISPLAY=:0. The Alroot-specific launchers install safely with:

    bash ~/alroot_wayland/tools/install-cli.sh
    aw-mpd /usr/bin/xdpyinfo
    aw-mprun /usr/bin/xdpyinfo
    aw-pdrun /usr/bin/xdpyinfo

Inside the Alroot XFCE session, the original command names mpd, mprun,
and pdrun are resolved through a project-only PATH directory before
the global Termux binaries; they pass DISPLAY=:90 (or the actual isolated
display number) and an Xauthority path visible within the Debian rootfs.
The pdrun login environment sets DISPLAY=:0 unless overridden *after*
login, which the project wrapper now does. Its unconditional xhost +
invocation is replaced with a project-process-local no-op; the X11
server ACL remains enabled and original Termux-X11 is unchanged.

Each wrapper's inherited DISPLAY was checked without xdpyinfo -display,
and disposable X11 GUI xmessage probes were opened on :90. A synthetic
GUI app is not a full GPU-accelerated Linux application compatibility
test; later stages will test real PRoot/Mesa workloads separately.

## P04: low-latency software IME, PiP, touchpad and localization

The legacy X11 IME bridge called xclip and xdotool for every Hangul
composition update, waited 350ms to restore the clipboard, and polled
every 500ms. P04 moves the hot path to a persistent UNIX-domain socket
and Xlib/XTEST service. Android sends messages from a HandlerThread, not
the UI thread, and coalesces redundant preedit updates over 8ms.
The native service updates changed UTF-8 suffixes and handles actual
SelectionRequests before restoring the previous plain-text clipboard.
The legacy queue is retained as a logged fallback if the service fails.

S26U standalone correctness and latency test in private Xvfb (not the
user's running :0 or :90):
- 40 rapid synthetic Hangul composition operations, GTK final output
  exactly ten syllables, no delivery failures.
- A disposable xfce4-terminal got exactly two synthetic Korean characters.
  Its Ctrl+Shift+V path requires handling multiple SelectionRequests.
- A synthetic 8950-byte clipboard was restored byte-for-byte.
- Native bridge average operation processing time measured about
  1–6ms across the tested paths. This excludes real Android IME delivery
  and pixel presentation. The physical software keyboard must still be
  tested with the newly installed APK.

To run the safe noninteractive test (xorg-server-xvfb package required):

    bash ~/alroot_wayland/tests/p04-ime-headless.sh

Tap jitter is reduced: relative pointer motion inside Android's
touchSlop is ignored, and initial movement beyond the threshold sends
only the intentional excess. Pointer speed/acceleration remain user-set.

PiP:
- Native Android picture-in-picture and resizable Activity support,
  manual PiP menu entry, 16:9 aspect ratio.
- Auto-PiP when leaving for Home (enabled by default) can be toggled.
  Android 12+ uses autoEnterEnabled; Android 8–11 uses onUserLeaveHint.
- The overlay and pointer capture are hidden/released in PiP and
  restored after exiting, without stopping the Wayland service.
- Actual PiP creation and display continuity are pending device tests.

Menu, dialog and notification strings use Android values (English) and
values-ko (Korean). Android resolves Korean automatically according to
the device/app language; English is the fallback.

After updating to the latest signed APK, run:

    awstart --status
    awstart --debug
    logcat -d -s AlrootWayland | grep -E 'IME persistent|IME socket sent|PiP'
    awstart --logs

Verify real Samsung keyboard Hangul composition and deletion in Mousepad
and XFCE terminal, pointer behavior on short taps, PiP on Home/reopen and
Korean menu text. An isolated test or successful APK build does not prove
real-device UI correctness.

## P05: native Unicode IME, isolated XFCE indicators, runtime naming

P04 still used the X11 CLIPBOARD for every Hangul composition, briefly
changing the selection owner. This triggered Android clipboard notifications
and could replace selected text with clipboard contents. P05 replaces
Unicode input with the persistent X11 XTEST daemon's reserved spare
keycode, mapping each UTF-8 Unicode scalar directly to an X11 key event.
It NEVER reads, takes ownership of, or modifies CLIPBOARD. The isolated
Xvfb GTK entry and xfce4-terminal synthetic tests both pass, with the
pre-existing clipboard preserved. Real Samsung keyboard input must
still be tested after installing the APK.

The original XFCE battery GenMon plugin was registered but not reliably
visible. P05 adds a separate project-only GTK battery status icon showing
the Android BatteryManager percentage. It was confirmed embedded in the
Alroot Xwayland system tray. XFCE Notes autostart is also copied and
retargeted to a second Alroot-only process, DISPLAY :90. Notes and
configuration data are snapshot-copied into the private project directory
on first use. They are not shared writable with the original :0 notes.

All shipped Wayland output manufacturer/model, seat, JNI symbols, log
identifiers, native function prefixes and native shared-library name
are Alroot-branded. Original GPLv3 attribution remains in LICENSING.md
and LICENSES/. The original upstream provenance is still auditable.

Termux's original debian launcher continues to target Termux-X11 :0,
which can report about 30Hz through XRandR. It is not an Alroot display.
Use aw-debian from native Termux to target Alroot Xwayland, or run debian
within the Alroot XFCE terminal (project-only PATH). Neither changes
the original Termux-X11 launcher or its display:

    aw-debian
    fastfetch --structure Display

The Alroot XRandR synchronizer now uses server-accepted CVT timings
instead of a modeline which Xwayland rejected. The Android host's
Wayland CURRENT mode remains the physical refresh authority; a modeline
alone does not prove matching real frame presentation cadence.

The current sharedUserId-based development helper cannot install
independently of a matching Termux APK because it inherits Termux's
UID, signature and app-private executable/rootfs paths. A future
standalone APK is possible, but requires bundling the runtime/rootfs
and replacing the Termux app-data paths and shared-UID relationship.
Do not remove sharedUserId or change signing until that independent
product architecture is implemented.

## P06: two-finger pointer, Hangul, neutral Display label and battery

Touchpad: once a second finger lands, single-pointer relative motion
remains disabled until all fingers lift and a fresh DOWN arrives.
POINTER_UP no longer switches the primary pointer onto the surviving
finger with stale touchStartX/Y. Two-finger scroll starts only outside
Android touchSlop and accumulates fractional motion. Real touch gestures
still need a physical S26U UI test.

Hangul: the P05 XTEST bridge reused one blank X11 keycode for every new
Unicode syllable. A rapid 가나다... burst previously produced the first
syllable followed by copies of its last syllable because GTK decoded
pending events with the newest key mapping. P06 caches Unicode symbols
over a pool of unused Xwayland keycodes and reuses each mapping where
possible. Rapid sequences of 10 and 24 DISTINCT syllables now matched
exactly in isolated headless GTK, and P04 terminal tests still passed.
This changes only project-owned Xwayland keymaps, never Termux-X11 :0.

Display: Xwayland initially put the custom CVT timing under disconnected
WL-1. The old refresh synchronizer searched all outputs and mistook it
for an attached XWAYLAND0 mode. P06 filters connected-output modes and
waits for a running XFCE window manager and panel before syncing.
S26U then selected a 1920x1080_120Hz XRandR mode at 119.88Hz while
the Android host wl_output was at 120000mHz. Real per-application
presented FPS remains a separate measurement.

Fastfetch: the Alroot-only Debian config preserves the user's module
ordering but displays resolution and measured XRandR rate under the
generic label Display, without XWAYLAND0 or Alroot branding. The native
Wayland output now identifies generically as Android/Built-in display.
The original Termux-X11 :0 Debian launcher and user fastfetch configs
are unchanged. From native Termux use aw-debian fastfetch; in the Alroot
XFCE shell use its project-specific debian command.

Battery: use the original XFCE panel configuration, including plugin-7
of type battery at the original index, instead of the tiny custom tray
icon or GenMon plugin. Alroot and native Termux-X11 both loaded the
same libbattery.so wrapper. Actual percentage visibility is a physical
device UI acceptance item.

Project-only regression tests:

    bash ~/alroot_wayland/tests/p06-ime-distinct.sh
    bash ~/alroot_wayland/tests/p06-smoke.sh

## P07: Korean commit-only IME, private UPower and natural touchpad scroll

The P06 native XTEST bridge corrected its Unicode keycode cache, but the
Android bridge still sent EVERY mutable Hangul preedit to the remote
application. Each preedit replacement required an X11 Backspace and then
new key events. This caused visible caret flicker on input and deletion,
and could replay already entered English when switching keyboard languages.

P07 adds a pure-Java ImeCommitState in the Android app. The mutable Hangul
preedit remains in the Samsung/Android IME; only explicit commits and
immutable syllable prefixes are sent to X11. English text is immediate,
and a narrowly scoped Korean-language-transition check suppresses
accidental replay of the last already-committed ASCII run. Pending preedit
Backspace is handled locally, so it cannot delete adjacent committed X11
text. An empty Samsung commit finalizes the last pending syllable once.
No X11 clipboard or speculative remote deletion is involved.

This has an important UX limit: an unfinished Korean syllable is displayed
in the Android keyboard's composition area until confirmed, not rewritten
repeatedly inside the GTK application's actual caret. Real in-place
non-flickering preedit would ultimately require native XIM/GTK input-method
integration, beyond an XTEST-only compatibility bridge. Acceptance on
the device must check that Korean text is neither lost nor duplicated and
that the final character is committed when the keyboard is dismissed.

The original XFCE battery plugin at plugin-7 remains in precisely the
same position/size as the original Termux-X11 setup. Its UPower proxy
previously failed because Android Termux has no system_bus_socket and
the Alroot session bus is isolated. P07 starts a small project-private,
read-only org.freedesktop.UPower-compatible provider using the Android
helper BatteryManager JSON snapshot. Only the child Xfce session maps
DBUS_SYSTEM_BUS_ADDRESS to its private DBus session. Neither the original
Termux-X11 UPower service nor its DBus/system settings are changed.
The project libbattery.so wrapper's UPower Percentage property was
confirmed to match the Android battery cache in a live S26U test.

Two-finger scroll has been reversed to Android-style natural scrolling:
dragging fingers DOWN causes content to move DOWN, corresponding to a
negative Wayland vertical scroll axis. Activation touchSlop, fractional
accumulation and two-finger tap/right-click are otherwise preserved.

P07 unit and live-session checks:

    bash ~/alroot_wayland/tests/p07-smoke.sh
    bash ~/alroot_wayland/tests/p07-upower-live.sh

The tests validate actual Java IME state-machine scenarios, simulated
touchpad scroll signs, Xwayland Unicode input, original XFCE panel
settings, and live private UPower properties. They do NOT prove the
actual Samsung keyboard's in-app appearance or finger gestures on
the device. Test these after the signed APK update.
