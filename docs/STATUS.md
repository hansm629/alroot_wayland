# P01 status (2026-10-27; update against actual evidence)

| Scope | Result | Evidence / limitation |
|---|---|---|
| Public repo | PASS | hansm629/alroot_wayland created |
| Native source import | PASS | DroidDeck pinned 0f839bef; build unverified |
| Termux compatibility | PASS | UID 10400 and installed APK/candidate signing cert match |
| Existing X11 preserved | PASS | no reset or package upgrades |
| Labwc | BLOCKED | Termux libtermuxgui protobuf ReadCord symbol mismatch |
| Xwayland | TEST-PENDING | installed; rootful options verified |
| CI APK | TEST-PENDING | run/compile/sign evidence not yet captured |
| S26U helper display | TEST-PENDING | session and actual surface not yet proven |
| Keyboard/capture/IME/clipboard | TEST-PENDING | API wiring not device verification |
| Service persistence | TEST-PENDING | recreate/DeX/offscreen untested |
| CPU top-app lineage | TEST-PENDING | shared UID not a CPU scheduling proof |
| GPU-copy/zero-copy | NOT STARTED | P03-P08, not P01 |

Do not promote code existence or build success into screen/input PASS.
