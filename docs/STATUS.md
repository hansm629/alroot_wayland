# P01 status (2026-10-27; update against actual evidence)

| Scope | Result | Evidence / limitation |
|---|---|---|
| Public repo | PASS | hansm629/alroot_wayland created |
| Native source import | PASS | DroidDeck pinned 0f839bef; JNI/Vulkan native component built by CI |
| Termux compatibility | PASS | UID 10400 and installed APK/candidate signing cert match |
| Existing X11 preserved | PASS | no reset or package upgrades |
| Labwc | PARTIAL / TEST-PENDING | private upstream wlroots 0.18.2 built and Labwc help works; nested session not yet verified |
| Xwayland | TEST-PENDING | installed; rootful options verified |
| CI APK | PASS (unsigned) | Actions run 40510004620 built app-release-unsigned.apk, native JNI included |
| local APK signing | PASS | locally signed from CI artifact, v2/v3 verified, installed Termux cert matched |
| S26U helper display | TEST-PENDING | session and actual surface not yet proven |
| Cursor/capture | TEST-PENDING | guest cursor/hotspot and host escape path wired; screen test pending |
| Clipboard | TEST-PENDING | Android/X11 bounded private IPC and native Wayland path wired; live copy pending |
| Keyboard/IME | TEST-PENDING | physical scan codes, X11 XTEST commit and bounded deletion wired; Hangul pending |
| Service persistence | TEST-PENDING | recreate/DeX/offscreen untested |
| CPU top-app lineage | TEST-PENDING | shared UID not a CPU scheduling proof |
| GPU-copy/zero-copy | NOT STARTED | P03-P08, not P01 |

Do not promote code existence or build success into screen/input PASS.

## S26U awstart actual shell integration
- awstart installed as a new non-overwriting Termux CLI shortcut; help, doctor, dry run and smoke tests succeeded.
- S26U nested Labwc: host socket READY, Labwc and project-owned rootful Xwayland :90, XFCE, clipboard processes RUNNING.
- Private wlroots Android patch now also fixes Termux X11 socket paths (system lib untouched).
- Rootful Xwayland now stays alive after readiness probe: removed -terminate.
- Guest P01 initial rootful output size 1280x720; APK size synchronization is P02.
- Android host logs have compositor/client frames but GUI/capture/IME/clipboard user confirmation is still TEST-PENDING.
- New foreground Activity handoff requires app version 2 update; top-app cgroup/cpuset worker policy remains TEST-PENDING.
- User's original Termux-X11 :0 and XFCE process survived session tests.

## Installer status / failed-start follow-up
- S26U PackageManager now returns com.alroot.wayland on shared UID 10400. Installed base.apk and the local signed P01 APK match byte-for-byte, including the intended Termux signing certificate.
- The user's earlier awstart command saw NOT INSTALLED; installation was subsequently confirmed. The reason the earlier lookup was empty was not independently determined.
- Actual default awstart --debug returned 0: foreground Activity acknowledged launch; host READY and Labwc/Xwayland/XFCE/clipboard RUNNING at isolated X DISPLAY :90.
- The installer command now distinguishes opening the Android installer from confirmed installation and avoids reopening it when local and installed APKs match.
- On-screen correctness, capture, Korean IME and clipboard remain TEST-PENDING.

## Black-screen investigation: 2026-10-27

- **Correction**: process and host frame counters were wrongly treated as
  evidence of successful XFCE display. The user saw an actual black screen.
- Isolated Xwayland :90 had no X11 window-manager registration. Its xfdesktop
  and xfce4-panel processes selected native GTK Wayland and logged missing
  layer-shell errors; the only xfwm4 was running for the unrelated original
  Termux-X11 session on :0.
- The project-only XFCE environment now forces GDK_BACKEND=x11,
  QT_QPA_PLATFORM=xcb, SDL_VIDEODRIVER=x11, DESKTOP_SESSION=xfce and
  XDG_CURRENT_DESKTOP=XFCE. No global environment or Mesa is changed.
- After a project-only restart, xfwm4, xfdesktop and xfce4-panel were
  registered on isolated :90 at 1280x720 with their actual X11 WM_CLASS;
  prior GTK Wayland/layer-shell warnings disappeared.
- An Xlib XGetImage probe read 576/576 nonblack sample pixels from each of
  isolated X11 root, desktop and panel. This proves nonblack source X11
  pixels, **not yet their presentation on the Android SurfaceView**.
- x11_desktop: READY/INCOMPLETE now verifies the project's real X11 WM,
  desktop and panel registration before awstart reports session readiness.
  Mocked missing WM correctly reported INCOMPLETE.
- Transient empty pm path queries can misreport an installed APK as missing.
  awstart now falls back to package-list evidence, and the explicit --open
  action relies on Android ActivityManager's actual result.
- APK v0.1.2 adds lightweight PixelCopy sampling of the helper's own
  SurfaceView, logging nonblack pixel counts and brightness range. A real
  PixelCopy run and final display correctness remain TEST-PENDING until the
  Android-approved APK update.

## 2026-10-28 P02 revision 2 (S26U)

- User screenshot: distorted fullscreen 16:9, 96-DPI expectation not met, Android soft input missing, Plank layout different, battery widget BAT --%, stray cursor cross. These were treated as reproduction evidence, not resolved by old process counters.
- Rootful Xwayland was hardcoded to 1280x720 despite Android guest setting 1920x1080. Separate server now starts with configured guest geometry, explicitly -dpi 96; isolated xdpyinfo verified 1920x1080 and 96x96 DPI. Fullscreen compositor changed from STRETCH to aspect-preserving FIT (landscape height fills, side margins may remain). Android visible result pending updated APK installation.
- Termux-X11 Plank icon order/theme/zoom came from ~/.config/dconf/user, not merely .dockitem launchers; original and isolated DConf SHA256 matched in live scoped test. Refresh private Xfce panel/theme/desktop snapshot each start. Original user settings were not modified.
- Battery GenMon's direct Termux API calls were unreliable in the panel; Android helper now publishes a bounded BatteryManager JSON snapshot periodically and GenMon reads it with a three-minute freshness check (Termux API fallback). Java cache production in newly installed APK remains unverified; battery fixture passed.
- A disposable X11 test revealed `xdotool type` Korean produced keysym=0 despite successful XTEST send. New Unicode commit via X11 clipboard paste was verified in a GTK entry and XFCE terminal on isolated :90 (2 Korean chars / 6 UTF8 bytes); the previous CLIPBOARD hash matched after restoration. Android keyboard source path is not yet verified.
- Removed temporary CursorOverlay white cross (the genuine Wayland cursor remains).
- CI and Android visual validation statuses should be recorded only after each actual run.

## 2026-10-28 P03 fundamentals (awaiting the new APK)

- Refresh: prior native output mode remained 60000 mHz despite Android
  host perf reporting about 1200 VSync ticks/10 seconds. The native
  Wayland compositor already supported dynamic mode notification.
  Added Android active Display refresh lookup, a seamless FIXED_SOURCE
  Surface rate request, and Choreographer measured-rate cross-check.
  Real Xwayland >60Hz mode and sustained frame-rate validation require
  installation of this new APK: do not call this PASS based on code alone.
- Input: user Android log showed commitText chars=0 and no nonempty text.
  The old fake Editable lost composing text. Replaced with synthetic
  X11-compatible InputConnection and incremental composition delivery,
  empty-commit finalization and Samsung/Gboard text-state responses.
  Android soft-keyboard end-to-end remains TEST-PENDING.
- Battery: Android BatteryManager cache was actually updated, and GenMon
  plugin id 107 plus /plugins/plugin-107 command were registered live.
  User still reported invisibility. Project-only panel now forces a
  persistent BAT label and live 15-second percentage updates. On-screen
  verification remains pending.
- PRoot: original mpd and mprun on :0 worked, mpd targeting :90 failed
  without explicit Xauthority. PRoot with an explicit rootfs-visible
  Xauthority successfully connected. Explicit aw-mpd, aw-mprun, aw-pdrun
  wrappers now target Alroot only; original binaries and :0 are intact.
- Important test correction: the first pdrun probe forced -display :90,
  hiding the original pdrun login profile resetting DISPLAY=:0.
  Added DISPLAY post-login. Inherited DISPLAY (no override) was then :90,
  and a real disposable pdrun xmessage GUI window appeared on :90.
  X11 access control remained enabled before and after all three tests.

## P04 2026-10-28 — next build

- Prior IME path was bounded by a 350ms clipboard restore wait and
  500ms shell polling, plus xclip/xdotool forks for every syllable.
  A persistent Xlib/XTEST UNIX socket service and Android background
  HandlerThread now handle IME events with short coalescing and delta text.
- Initial live exploratory test revealed a race with clipboard selection
  ownership. It was stopped immediately. All further testing was moved
  to private Xvfb instances :190–:195, not the user's existing displays.
- Synthetic 40-operation Hangul input passed with correct ten-character
  GTK text and zero failures. Terminal Ctrl+Shift+V multi-stage selection
  requests were fixed; isolated xfce4-terminal then passed its two-character
  Hangul test. The old synthetic 8950-byte clipboard was restored.
  Native handling averages roughly 1–6ms; Android end-to-end needs testing.
- PiP Activity manifest, manual entry, automatic Home entry, UI hide,
  pointer release and restore are implemented; screen testing pending.
- Relative touchpad taps now ignore sub-touchSlop jitter. Device
  gesture acceptance pending.
- English and Korean Android resources cover menus, dialogs and
  foreground-service notification. UI screenshot/locale test pending.

## P05 2026-10-28 — outstanding acceptance tests

- Korean input issue: user observed X11 clipboard copy toast and wrong
  selected-range replacement despite the P04 fast persistent socket.
  Source diagnosis: persistent native IME used X11 CLIPBOARD as paste
  transport. P05 rewrites native Unicode input using a blank XKB/X11
  keycode and XTEST with no CLIPBOARD involvement. Private Xvfb GTK and
  terminal tests passed. Android now refuses Unicode clipboard fallback
  if the persistent XTEST daemon is unavailable, preventing a recurrence
  of Android copy notifications and unintended pasted selections.
  On-device Samsung IME acceptance is pending.
- XFCE panel: GenMon is registered under plugin-107 and is running, but
  user reports missing visual output. Project-private GTK battery tray
  icon was confirmed XEmbed-embedded on the Alroot :90 XFCE panel.
  Original XFCE Notes runs only on :0; P05 prepares an independent
  Alroot :90 notes autostart with isolated data snapshot and configuration.
- Runtime identifier refactor: Wayland wl_output make/model, seat,
  log tags, JNI library/class and native prefixes are Alroot-based.
  Upstream copyright, original licenses and provenance are retained.
  New native build required to verify renamed ABI symbols.
- Debian 30Hz: original Termux shell has DISPLAY=:0; fastfetch measures
  original Termux-X11 XRandR ~29.95Hz. Alroot's separate :90 has distinct
  modes. Added aw-debian project-only launcher to select the Alroot X11
  server without modifying the original debian command; XRandR custom
  refresh mode validation continues.
- Standalone APK: review only. Development build still uses
  android:sharedUserId=com.termux and relies on matching Termux signing
  key, UID and executable HOME. A true independent APK needs app-private
  runtime packaging and environment redesign.

## P06 (2026-10-28): pre-release verification

- Touchpad: root cause of two-finger release jump was assigning the
  surviving pointer ID while keeping the departed pointer touchStartX/Y.
  After POINTER_DOWN, multi-touch keeps primaryPointer=-1 until a new
  gesture DOWN. Scroll has a touchSlop activation and fractional
  accumulation. Physical touch validation remains pending.
- Hangul: ten unique fast syllables were reproduced as the first syllable
  plus nine copies of the last; XSync alone did not resolve this.
  Using an X11 spare-keycode pool, 10 and 24 unique rapid Hangul glyphs
  passed the same isolated GTK test. P04 terminal/clipboard checks pass.
  Real Android Samsung IME testing remains pending updated APK.
- Refresh: physical Android parent 120000mHz; rootful Xwayland previously
  failed because custom 120Hz mode was created on disconnected WL-1.
  Connected-output filtering and delayed synchronization now resulted
  in selected XWAYLAND0 1920x1080_120Hz at 119.88Hz.
- Debian fastfetch: project-only config now gives a neutral Display
  label with actual rate, shown on S26U as 1920x1080 @ 120 Hz.
  Native Wayland metadata is generic Android Built-in display.
  The original Termux-X11 and user Debian settings remain intact.
- Battery: restored original XFCE panel XML byte-for-byte, including
  battery plugin-7 and its exact position; both project :90 and native
  :0 loaded libbattery.so plugin 7. Visual percentage needs user test.

## P07 2026-10-28 — IME composition, UPower, two-finger direction

- User-reported Korean text entry and deletion flickered, and switching
  from English input sometimes inserted old English a second time. Native
  XTEST transport was fast but the Android bridge sent mutable composition
  immediately, causing a destructive X11 Backspace+insert per preedit.
- Added Android-independent Java ImeCommitState: only confirmed text and
  immutable Hangul syllable prefixes reach the native X11 daemon. A
  narrowly bounded recent-ASCII check removes English echoes at the first
  Korean preedit transition. Local deletion of pending composition avoids
  accidental X11 Backspace. Eleven synthetic Java scenarios passed (including
  English→Korean, sequential Hangul, 0-length commits, finish/replay and
  backspace and direct no-preedit keyboard switching). The legacy ASCII
  preedit renderer remains immediate in X11
  so English composition responsiveness does not regress.
- This cannot produce fully inline animated GTK/XIM preedit: unfinished
  Korean syllables remain in the Android IME until committed. Actual
  Samsung keyboard UX and final-character preservation are TEST-PENDING.
- Native battery plugin still appeared as '?' under isolated Xfce because
  org.freedesktop.UPower proxy creation failed. The Android helper's battery
  JSON cache contained fresh percentages. Implemented a project-private
  UPower-compatible DBus service inside the isolated dbus-run-session.
  Alroot's native libbattery.so now inherited its own mapped D-Bus system
  address, and the live D-Bus Percentage equalled the Android cache.
  Original Termux-X11 and DBus services were left unchanged.
- Two-finger scroll uses the natural Android direction (negative Wayland
  vertical axis for positive finger Y). TouchpadScrollState simulated
  direction/dead-zone cases passed; actual finger acceptance is pending.
- P07 smoke includes the above Java tests and the live Alroot private
  UPower battery property check, plus retained P06 Xvfb Unicode tests.

## P08 2026-10-28 — additional regression root causes

- English-to-Korean echo: P07 alreadyInsertedAscii overwrote the
  recent English history with the last commit, losing words committed
  as one character at a time. P08 accumulates the last English run,
  filters matching echoed prefixes on Hangul composition and commit,
  and suppresses stale whole-word ASCII composition after finalization.
  InputConnection now sends fake-cursor selection updates through
  balanced IME batch edits, matching Termux-X11 conventions. No
  plaintext keystrokes, clipboard or history are written to logs.
- Korean caret: mutable Korean preedit stays local to the Android IME;
  only immutable/committed text is emitted to XTEST. GTK-native inline
  preedit/XIM remains a follow-on enhancement if truly equivalent
  Termux-X11 visual composition is required. Physical test pending.
- Battery question-mark: real installed Termux battery plugin source
  forks termux-battery-status, not just UPower Percentage. The plugin's
  background API invocation was unreliable inside isolated Alroot.
  Project-private PATH now resolves that program to a fast Android
  BatteryManager JSON cache shim. The original plugin config and
  native Termux executable remain untouched. The patched libacpi.c
  parser was compiled and validated on S26U against this JSON.
- Wallpaper: mirror_xfce overwrote xfce4-desktop.xml plus xfce4/desktop
  on every restart. These paths are now copied only for clean first
  launch. A synthetic-home test confirms repeated restarts preserve
  the Alroot wallpaper and desktop icons. The original :0 is untouched.
- Only successful test/build evidence may be marked PASS; physical
  screen/keyboard acceptance remains pending until new APK update.

## P09 (2026-10-28): real-time Hangul regression repair

- P08 battery percentage and wallpaper persistence confirmed by user.
  Those functional paths are retained unchanged.
- Korean typing/deleting became very delayed after English-to-Korean
  switching: P07/P08 Java commit-only IME state withheld mutable
  preedit from X11. Native daemon event processing was sub-millisecond
  but had no composing requests to render.
- P09 now streams each changed Korean composing string immediately as a
  native C operation; finalized text uses T. Backspace revises the
  currently rendered preedit, not merely a Java-side draft.
- P08 English replay filtering/synthetic cursor, no clipboard typing
  fallback, Unicode spare key pool remain intact. The native daemon
  prewarms Unicode mapping before enqueueing Backspace to avoid
  a separate intermediate XFlush frame.
- Added a short cross-source duplicate DEL guard for IMEs that call both
  sendKeyEvent and deleteSurroundingText for one keypress.
- S26U private headless tests: 20 JVM event scenarios, 7 duplicate
  Backspace scenarios, Java → socket → XTEST → Xvfb GTK with first Hangul
  visible around 1–2ms after synthetic send and correct final text,
  plus P04 GTK/terminal and P06 distinct Unicode regression tests.
  This is NOT physical Samsung keyboard-to-pixel evidence.
- Native IO HandlerThread queue delay now logged in aggregate without
  typed text. Physical keyboard acceptance is TEST-PENDING after APK.

## P10 2026-10-28 — residual Korean cursor redraw smoothing

- User confirms P09 critical Hangul input delay fixed, but occasional
  caret flicker/stutter remains, especially during composition delete.
  Battery, wallpaper, PiP, native Termux-X11 and input responsiveness
  must not regress.
- Native IME socket queue measured ~0.1ms Java worker latency and native
  processing averages well under 2ms, so simply increasing timeouts
  or reverting to commit-only Hangul would worsen actual responsiveness.
- A private Xvfb/GTK visual-state test reproduced an empty *redraw*
  when C("") was dispatched between C("한") and C("하").
  Direct replacement preserved the correct text without an empty
  GTK draw, even though multiple internal changed events occur.
- P10 defers only an empty Korean composing/delete revision for 9ms
  (about one 120Hz frame). Any following revised preedit cancels that
  temporary erase; all nonempty Korean composing and English events
  remain immediate. Ordered flush on committed-DEL, finish/focus and
  a second Backspace avoids stale events and accidental extra deletion.
- Removed per-Unicode-key XSync round trips, preserving necessary
  keymap synchronization and one operation-level flush.
- Headless tests of both deletion APIs yielded final correct Korean
  glyph and zero painted intermediate blank frames. Previous P09
  Java/native/GTK, P04 terminal and P06 10/24 distinct Korean tests
  still passed. Physical Samsung/One UI acceptance is TEST-PENDING.
- XTEST remains a keypress compatibility layer, not true XIM preedit.
  It cannot guarantee zero GTK internal change notifications or
  eliminate every possible visual cursor animation. True inline
  composition would require later XIM/GTK bridge work.
- P04 refresh stability: Choreographer measured timestamp jitter briefly
  caused the older code to advertise 122Hz despite the physical mode
  remaining 120Hz. This release retains the actual Android active physical
  Display rate as the wl_output authority; timing measurements remain
  diagnostic only, preventing transient nonphysical modes.
