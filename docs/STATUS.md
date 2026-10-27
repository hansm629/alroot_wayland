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
