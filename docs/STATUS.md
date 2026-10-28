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

## P11 2026-10-28 — Fcitx5 native preedit integrated

- User P10 acceptance: serious Korean input latency was fixed, but
  cursor movement/flickering remained while composing/deleting Korean.
  XTEST Backspace+Unicode cannot truly emulate GTK preedit.
- New Termux-native, project-private Fcitx5 addon uses focused input
  context's clientPreedit/updatePreedit and commitString methods.
  The broker preserves existing XTEST for English, raw Xlib/unsupported
  clients, and committed-text deletion. Reply acknowledgments prevent
  double delivery; no clipboard access or typed-text logging.
- Project-only Fcitx5 build, private XFCE session GTK/Qt input module
  environment, automatic addon loading and opt-out preference via
  awstart --ime-xtest were implemented. No global Fcitx5 changes.
- On isolated Xvfb/D-Bus, native GTK saw three preedit notifications
  and only ONE real text change at commit. XFCE VTE received Hangul
  correctly. English unchanged XTEST and raw Xlib fallback passed;
  switching GTK -> Xlib -> GTK worked with zero uncertain deliveries.
- Extra native GTK edit checks passed for committed Korean deletion
  followed by another syllable, and English-to-Korean no-duplication.
  Previous P10 XTEST-only and P08 battery/wallpaper tests were retained.
- On live S26U Alroot :90 (not just Xvfb) the addon autoloaded,
  correct GTK/terminal native preedit and raw Xlib Korean/ASCII XTEST
  fallback were observed. P10 battery, wallpaper, PiP, real 120Hz
  display mode, and original Termux-X11 :0 remained untouched.
- User's actual Samsung touch-keyboard UI and visual cursor flicker
  acceptance are still TEST-PENDING after the new signed APK.

## P12 2026-10-28: Labwc output regression and Korean DEL English echo

- User reports P11 native Fcitx Hangul improved but XFCE appeared
  oversized and old English resurfaced after Korean Backspace.
- Confirmed both Termux-X11 :0 and Alroot :90 have 1920x1080 96 DPI,
  identical GTK fonts/theme/scale and byte-identical panel xsettings.
  Nested Labwc wayland-1 was actually 1280x720, stretched to full
  phone height; host wayland-0 was 1920x1080. Direct project-only
  wlr-output-management custom mode changed WL-1 to 1920x1080
  successfully; Android log thereafter reported a 1920x1080 scene.
- Added a source-built project-private output mode helper executed
  before Xwayland/XFCE startup, with geometry from the existing
  validated output-resolution file. No global package/wlroots change
  or fake reduction of the Xft DPI value.
- Added reproducible JVM regression: English typed first, >1.2s
  Korean composition, Korean deleted, then Samsung stale English C/T.
  P11 initially FAILED; a short echo guard measured from the Korean
  deletion now prevents duplicate English. Focus change/normal new
  English input is not suppressed. Both Android KEYCODE_DEL and
  deleteSurroundingText update the same state before sending X11 DEL.
- Added full private native Fcitx GTK roundtrip for this exact event
  sequence: GTK final text remained 'hello', with no duplicate English;
  native/legacy routing metrics reported zero uncertain deliveries.
  Separate Android model tests include explicit finish and focus reset.
- Live S26U Alroot project restart reported nested Labwc 1920x1080
  at 96 DPI, Xwayland 1920x1080, Android physical 120Hz mode and
  Fcitx addon ready. Battery, wallpaper, PiP and original Termux-X11
  were not modified. Actual Samsung keyboard acceptance still pending.

## P13 2026-10-28 — native caret style and English-after-Hangul-DEL regression

- User reports P12 still duplicated prior English after Korean delete,
  and native Korean composition had a visible underline/no normal caret.
- Native Fcitx addon explicitly requested Underline; changed to NoFlag
  and preserved documented byte-accurate preedit cursor position.
  An actual private GTK screenshot shows no underline and a visible
  vertical insertion caret. Theme-specific on-device acceptance pending.
- Native IME broker previously handled C/T with Fcitx but always
  executed committed D via XTEST, potentially feeding synthetic keys
  through Fcitx again. Implemented native committed-text delete for
  focused clients with SurroundingText, retaining XTEST as fallback.
  Private GTK test recorded native_del=2, xtest_del=0, uncertain=0.
- Android InputConnection was missing Termux-X11's requestedPos/
  phantom-whitespace delete guard. Added Android-independent
  ImeCursorTracker with batch edits and no stored typed text.
  Unaccepted stale commits do not advance synthetic cursor position.
- Expanded private JVM plus GTK sequence to exercise
  English→Hangul→phantom cleanup→real DEL→old English replay.
  Native Fcitx/GTK, English and raw-Xlib XTEST fallback all passed.
  Actual Samsung keyboard behavior remains TEST-PENDING.
- P04 refresh stability: Choreographer measured timestamp jitter briefly
  caused the older code to advertise 122Hz despite the physical mode
  remaining 120Hz. This release retains the actual Android active physical
  Display rate as the wl_output authority; timing measurements remain
  diagnostic only, preventing transient nonphysical modes.

## P14 2026-10-28 — scoped native Hangul caret renderer in VTE

- P13 user acceptance: Korean typing/deletion nearly perfect, but the
  ordinary insertion caret disappears during native Hangul composing.
- Reproduced the issue in both a pure GTK3/VTE test and XFCE terminal,
  while GTK text-entry widgets showed a normal caret. In pinned VTE
  0.84.1, paint_cursor skips preedit, and paint_im_preedit_string draws
  a caret only for preedit_cursor less than len. Native Fcitx5 correctly
  reports a position equal to len after the last Hangul character.
- Patched only the preedit caret drawing logic, keeping all Fcitx5 and
  Android input, text, clipboard and GPU paths unchanged. Builds
  LGPL-compliant project-only VTE with GnuTLS, ICU and a11y support.
- Verified private Xvfb original-vs-patched screenshots: composing
  differed by exactly 84 pixels at the insertion caret; committed
  display was byte-for-byte pixel identical. Visual regression suite
  now lives at tests/p14-vte-caret-smoke.sh.
- Project-only XFCE terminal wrapper loads private VTE at Alroot :90
  or :90.0. Live /proc process map verified the patched library. The
  installed native Termux libvte SHA256 and original X11 :0 remained
  unchanged. Existing terminal server processes are never killed.
- The Android helper APK remains P13 v0.1.15. P14 needs no new APK,
  only the scoped VTE build/launcher and a new XFCE terminal window.

## P15 2026-10-28 — Android soft Shift and XTEST uppercase preservation

- Follow-up user issue: Android software keyboard did not produce English
  uppercase characters. Reproduced the native X11 half of the defect on a
  private Xvfb GTK editor: `aAZz!?` became `aazz!?`. Xwayland/XKB case-folded
  level-0 uppercase keysyms installed by the XTEST Unicode spare-keycode
  mapper. This was not a Hangul/Fcitx5 or VTE rendering defect.
- The native XTEST ASCII uppercase path now sends a scoped Shift+letter
  down/up sequence, while preserving the existing lowercase, symbol,
  non-ASCII Unicode and native Fcitx5 Hangul routes. A new isolated GTK
  regression shows six exact characters `aAZz!?`, with no duplication.
- Android's virtual scanCode=0 KeyEvent fallback previously forwarded only
  the alphabet's evdev code and discarded META_SHIFT. It now synthesizes a
  bounded press/release sequence for uppercase letters *only* when no real
  guest Shift or Ctrl/Alt/Meta shortcut is held. An optional later IME
  key-up is consumed, avoiding key/modifier latch. Hardware keys, normal
  lowercase and commitText/setComposingText remain on their existing paths.
- `tests/p15-uppercase-smoke.sh` checks the scoped virtual-Shift policy and
  live XTEST-to-GTK capitalization in isolated Xvfb. P13 and P14 regression
  suites are preserved. Android helper advances to v0.1.16 (code 17).
- Real S26U Samsung keyboard Shift, double-Shift, English→Korean→DEL and
  GUI caret appearance require updated APK installation and user test;
  a successful JVM/Xvfb/CI test alone is not on-device acceptance.

## P16 2026-10-28 — stale ASCII after Hangul deletion and initial pointer

- User confirms P15 uppercase and P14 visible Korean caret are correct,
  but past English may still reappear after composing/deleting Korean.
  They also observe a cursor at the top-left before XFCE finishes, then
  a spontaneous jump to the display center.
- A failing Java regression reproduced the precise missed prefix case:
  delivered 'hello' + composing 'hel한' yielded 'hellohel한'. The prior
  filter only matched a suffix of already committed English; a
  partial prefix escaped. It now checks both without changing the
  ordinary native Fcitx Hangul C/T route.
- Samsung can issue an empty C to clear Hangul before reporting DEL.
  A short, one-use Korean-preedit deletion marker prevents that same
  DEL from erasing previously committed Latin text. The Korean editing
  context remains armed briefly even when the intermediate draft is
  already empty. The old English echo filter no longer misses that case.
- A progressively rebuilt old ASCII snapshot ('h'→'he'→'hello')
  following Hangul deletion is withheld before it reaches XTEST.
  Confirmed old text is dropped; a genuinely committed single new
  ASCII character remains accepted, and diverging fresh text uses the
  original immediate input path. This guard is scoped to the short
  Hangul-deletion boundary, not normal English typing or other editors.
- The Wayland host previously started at pointer coordinates (0,0);
  Android's uninitialized cursor overlay also defaulted to (0,0).
  The native seat now starts centered and sends its first enter/motion
  once a real desktop or mapped toplevel and pointer resource exist.
  Initial centering uses actual scene dimensions, and any user
  pointer event disables subsequent bootstrap, avoiding late warps.
  Android mirrors the visual center when its surface is first sized,
  without generating a premature guest move or resetting a moved
  pointer on ordinary resize. Capture/touch remains the authoritative
  first actual pointer action when reattaching an Activity.
- Regression coverage: extended Java event sequences; an actual
  private-Xvfb GTK/Fcitx/native-socket P16 prefix-replay case; P13
  Korean edits, P14 VTE cursor and P15 uppercase smoke tests. None
  inject into the user's live :0/:90 sessions.
- APK v0.1.17 (versionCode 18). Pointer visual timing and real Samsung
  IME on the newly installed APK remain TEST-PENDING until user review;
  do not promote CI/native Xvfb success to physical display PASS.

## P17 2026-10-28 — Android IME stale-editor feedback correction

- User confirms P16 pointer centering fixed, but English → Korean →
  delete Korean still duplicates earlier English on the *real* S26U.
  P16 APK is installed and matching; this is a genuine P16 regression
  failure, regardless of its green synthetic Xvfb test results.
- Real Android logcat shows Samsung's previously inserted English is
  filtered more than 70 times in a single typing sequence, with repeated
  events approximately every 50ms. The focused real X11 client was
  xfce4-terminal; the X11 daemon also reported XTEST DEL when the client
  lacks Fcitx5 SurroundingText. Native GTK and VTE support differ.
- Reproduced an actual P16 state-machine defect with a FAILING Java test:
  after rejecting a one-character old English composing prefix,
  takeWithheldAsciiOnFinish() returned that one character and the bridge
  sent it as a *new commit*. Now any unconfirmed matching old prefix
  is discarded on finish instead; a genuine commitText of a single
  newly entered letter is still accepted.
- P16's 3.5-second *absolute* stale-replay cutoff was insufficient
  against ongoing old-text callbacks. The guard is now extended by
  each confirmed old-text retry and disarmed by accepted fresh direct
  ASCII, new actual committed content, editor reset, or a 5-second
  interval with no old-text replay. This avoids both delayed echoes
  and indefinitely rejecting intentional same-word re-entry.
- ImeCursorTracker no longer publishes the same selection repeatedly,
  including ignored stale commits, no-op batches and identical outside-
  batch setSelection. This breaks an Android→Samsung updateSelection
  feedback opportunity without changing the Fcitx5 native preedit
  buffer, visual VTE caret, Korean deletion semantics or DPI.
- ScanCode=0 ASCII KeyEvents, when actually forwarded to the guest,
  now contribute to the short in-memory English context without
  reinjecting their text. This closes a P16 blind spot when the Samsung
  keyboard uses sendKeyEvent instead of InputConnection.commitText.
- P17 logging records only aggregate counters for withheld preedits,
  rejected commits, direct ASCII-key events and redundant selection
  notifications, never actual text or clipboard content.
- Added a P17 case to the isolated Xvfb → native daemon → Fcitx5 →
  GTK test with stale ASCII on finish and continuously repeated old
  text beyond 3.5 seconds. Pure JVM cursor/input-state tests also
  cover all new branches. P14 caret and P15 uppercase retain
  their previous independently verifiable paths.
- Target APK v0.1.18, versionCode 19. As before, actual on-device
  Samsung-IME acceptance is TEST-PENDING until user verification.
  The known-working P16 pointer and P14/P15 input remain untouched.

## P18 2026-10-28 — first-editor state and startup pointer ownership

- User reports P17 still duplicates earlier English after deleting Korean
  on the FIRST attempt in each X11 input field; the initial pointer also
  briefly appears at the top-left before centering after XFCE loads.
  The earlier P16/P17 string-pattern filters have not resolved either
  physical-device issue and are not treated as proof of success.
- S26U logcat on P17 showed two InputConnection creation callbacks within
  the same millisecond of first keyboard focus, plus many early-IME
  resends. Before P18, onCreateInputConnection allocated a new anonymous
  BaseInputConnection and a new ImeCursorTracker on every call, resetting
  the Android fake editor even while Samsung had an existing connection.
  The existing Termux-X11 LorieView uses a persistent InputConnection.
- P18 uses one cached InputConnection per InputSurfaceView, preserving
  the fake cursor/composing state when Samsung requests it repeatedly.
  On true Activity/View recreation a distinct connection is created.
  EditorInfo no longer incorrectly advertises an explicit selection
  (1,1) in a nonexistent backing Editable; it uses unknown initial
  selection (-1,-1), consistent with the established Termux-X11 pattern.
- The host Wayland pointer already starts at scene center, but the Android
  onResolvePointerIcon used its independent system cursor whenever a
  guest cursor bitmap was not ready. In touchpad mode this could expose
  the OS pointer at (0,0) until Labwc supplied a guest cursor image.
  P18 hides the Android system cursor in touchpad mode and renders one
  Alroot cursor at the center as soon as view bounds exist, with a
  small temporary white arrow until the real wl_pointer cursor arrives.
  Touchpad motion stays on the project cursor; on the first actual
  physical-mouse event the original low-latency Android mouse cursor
  path is restored at its event position. Direct-touch/virtual-touchscreen,
  DeX hardware-mouse and pointer-lock behavior is preserved.
- Adds bounded diagnostic logcat records for up to 128 first-editor
  callbacks, recording only operation, text length, broad ASCII/Korean
  classification and delivery decision. No text, clipboard or keystroke
  contents are retained or logged. If first-use replay remains, these
  event categories can locate the actual failing transport rather than
  expanding guesses or heuristic echo windows.
- New Android-independent regression: persistent connection identity,
  stable fake cursor across repeated connection requests, first visible
  pointer centered and only one cursor drawn in touchpad mode. P13
  Hangul editing, P14 VTE caret, P15 uppercase and P16/P17 native IME
  regressions remain required.
- Target APK v0.1.19 (versionCode 20). Real S26U first-field IME and
  initial startup pointer acceptance are TEST-PENDING until observed.

## P19 2026-10-28 — remove oversized cursor and instrument real IME failure

- S26U user screenshot and P18 source show a grossly enlarged temporary
  arrow. P18 multiplied a hardcoded 19x27 cursor path by Android display
  density (around twice normal on a high-density handset); unlike the
  actual guest bitmap it did not obey the guest-to-view transform.
- P19 removes that Android density enlargement. The placeholder can now
  only be displayed at its original native-pixel dimensions or smaller,
  following the same scene-to-view scale as guest cursor imagery.
  The Activity root's Android pointer is hidden only in touchpad mode
  during the early hover window; the SurfaceView still restores the
  existing hardware mouse pointer after real mouse movement. Existing
  touch gestures and P16 centered host pointer coordinates are retained.
- Separate, disposable Xvfb/Fcitx5/real XFCE-VTE terminal regression now
  sends ASCII 'hello', native Hangul '한', then a committed deletion.
  PASS: final VTE line equals exactly 'hello', with XTEST_DEL=1.
  Therefore XTEST_DEL by itself does NOT reproduce the phone's
  first-field English reinsertion. Changing it or adding additional
  English substring filters without the real Android callback sequence
  would be speculative and risks a new regression.
- P18's volatile Android logcat ring lost the first-field event sequence
  before it could be inspected. P19 adds a bounded, private 64KiB rotating
  diagnostic at ~/.alroot-wayland/diagnostics/p19-ime-flow.log. It records
  only callback type, length, broad Latin/Hangul classification, local
  filter decision and X11 socket delivery; NEVER actual text, keycodes
  or clipboard contents. File writes use a separate handler thread and
  one held file descriptor so UI typing cannot block on each write.
- The project-local X11 daemon additionally prints its first 240 routing
  decisions (FCITX, XTEST, XTEST_DELETE, native delete, reset) without
  text contents to its existing project log. P19 Android flow + native
  routing is needed to identify exactly which real Samsung callback
  reinserts English on the first editor visit.
- P19 DOES NOT claim a final fix for the English duplication: the user
  has reported repeated physical-device failures through P18. This is
  intentionally a cursor regression repair and lossless, privacy-limited
  diagnosis before any new IME state-machine edits. P14 Korean caret,
  P15 uppercase, DPI, battery, PiP, XFCE and Termux-X11 :0 are unchanged.

## P20 2026-10-28 — real Samsung mixed-context replay and one cursor owner

- P19 user report: still two visible cursors (one independent Android
  pointer, the other P18/P19 native snapshot overlay) and repeated English
  text when deleting the final Hangul character.
- REAL S26U ~/.alroot-wayland/diagnostics/p19-ime-flow.log records the
  exact first-failure class (text itself was never logged):
    * Android ASCII C grows from length 1 to 27; the X11 daemon sends
      XTEST C, then Android F leaves that English committed/visible.
    * Samsung C contains the 27-character already-present Latin prefix
      plus Hangul, which ImeCommitState correctly strips while Hangul
      remains. Native Fcitx5 receives only the Korean suffix.
    * As Korean shrinks, the raw Android composing value becomes ASCII
      length 27 WITHOUT any deleteSurroundingText/KEYCODE_DEL first.
      P19 then calls composition.finished() -> native T(Korean), followed
      by XTEST C(27), retyping the earlier English. C(26..1) subsequently
      revises that new duplicate. The previous post-Backspace guard could
      never catch this transition.
- P20 records only the exact stripped Latin context of the *current*
  mixed Korean preedit and consumes its subsequent pure-ASCII revisions
  before the ordinary ASCII routing path. While Hangul is uncommitted,
  its Fcitx5 preedit is cleared by native C("") rather than committed
  with T. When Hangul is already committed (the real P19 T-then-C path),
  the stale ASCII revisions are ignored and a later real DEL removes
  just the committed Korean. The guard disarms when the old snapshot
  drains to empty, on a new/different ASCII preedit, direct fresh
  key input or a new editor. A trailing stale T is ignored once.
- Private Xvfb/Fcitx5/GTK native socket and JVM simulations now cover
  both uncommitted Hangul and already-committed Hangul followed by
  C(27..0). These demonstrate exact English retention without another
  XTEST ASCII insertion. Actual Samsung acceptance remains pending.
- Native compositor ROLE_CURSOR is NOT included in render_scene(): it
  publishes a bitmap for Android to draw/use as PointerIcon. Therefore
  the apparent two cursors came from Android hardware pointer and
  P18's always-on CursorOverlay, NOT two composited Labwc cursors.
  P20 reverts the P18 always-on overlay; startup uses a single Android
  hardware cursor. Actual touchpad gesture uses the Android overlay
  only when a guest bitmap is ready and suppresses the OS icon; actual
  physical mouse movement hands ownership back to the hardware cursor.
  The fake P18 arrow is fully removed. Native bootstrap tracks output
  mode changes before real pointer input so guest center does not use
  the obsolete initial 1280x720 mode; do not warp after user movement.
- Android cannot guarantee the initial *hardware pointer* position is
  centered without pointer capture/injection. P20 prioritizes one
  correctly-sized cursor over pretending a second centered cursor fixes
  the OS pointer. Startup center and Android icon suppression require
  actual S26U acceptance rather than synthetic tests.
- Keep Termux-X11 :0, user's XFCE settings, PiP, DPI, battery, native
  Korean composing caret, uppercase, pointer gestures unchanged.
