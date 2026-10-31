# S41: initialize the AHB rows exposed during vkcube resize

## Scope

S26 Ultra, 2026-10-31. The user accepted S40 glxgears and reported one remaining
vkcube issue: a thin black horizontal line near the bottom during continuous
window resizing. S41 selects a private Vulkan wrapper with initialized AHB
padding for the existing coalesced vkcube. The active server is the same S39
binary and the S40 glxgears launch route still uses S26.

## Why the line appeared

The S24 XCB path aligns the physical AHardwareBuffer height to four rows for
the KGSL import. A logical 320 x 301 image therefore has a 320 x 304 AHB-bound
destination image. Applications render the logical image, and the existing
GPU copy wrote only its 301 rows. DRI3 advertised all 304 physical rows.

During a resize, an older frame can reach a taller window before the next
swapchain frame. Rows 301 through 303 then lie inside the advertised source
pixmap, so Xwayland's uncovered-strip fill starts below them. Those rows had
never received valid source pixels. Device readback confirmed they were black
in this build; the logical edge was gray (`#333333`).

The diagnostic reproduced this using a genuine Vulkan-rendered frame. It
completed rendering, grew its own X11 window, presented the old-size image,
and required a real X Present COPY plus its matching Idle before reading
the source pixmap and the client window. This connects the source allocation
gap to the displayed X11 client rows, rather than inferring it from geometry.

## Change

`0005-s41-ahb-padding-edge-copy.patch` extends the existing
`wsi_cmd_blit_image_to_image()` copy with up to three one-row regions. Each
copies the last logical source row into one physical padding row. The original
logical rectangle is unchanged. The added destinations are disjoint.

The copy stays in the same command buffer, `CmdCopyImage` call, barriers and
submission. It executes on every Present copy, since the destination's prior
contents are discarded by its existing UNDEFINED transition. The change is
restricted to the Termux Android AHB image route with a matching width and
the expected one-to-three-row physical alignment gap. The original no-padding
and DXGI paths retain their one-region copy.

The production path adds no CPU pixel readback, queue wait, hard-coded
background color or resize-event handling. It preserves physical AHB alignment,
S26 transient-import retry, S39 Present lifetime handling and both coalesced
demo clients.

All image-transfer queue families reported `(1,1,1)` transfer granularity on
this device. The source image and AHB destination have separate allocations.
The Vulkan requirements for disjoint destination regions and source/destination
aliasing were checked against [vkCmdCopyImage](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImage.html)
and [VkQueueFamilyProperties](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html).

## Device measurements

### Exact boundary proof

All cases use a width of 320. The last logical source row must first be uniform,
nonblack gray; an all-black source frame cannot pass as correctly filled padding.

| Logical height -> window height | Physical height | S26 source padding and exposed window rows | S41 source padding and exposed window rows |
|---|---:|---|---|
| 301 -> 304 | 304 | Three black rows | All pixels match the logical edge |
| 302 -> 304 | 304 | Two black rows | All pixels match the logical edge |
| 303 -> 304 | 304 | One black row | All pixels match the logical edge |
| 301 -> 302 | 304 | Three source padding rows; one black window row | All source padding and the exposed window row match |
| 300 -> 301 | 300 | No source padding; Xwayland fills the extra window row correctly | Same correct control result |

Each corrected row matched at **320/320 pixels**, with **0/320 black pixels**.
Every diagnostic client exited with status 0. The live XFCE session retained
its original server PID 557 and display `:98` throughout these isolated tests.

Initial five-case receipts, under `.source/gpu-stage1/s41-padding-proof/logs/`:

- S26: `20261031-220850-31877/summary.json`.
- S41: `20261031-220958-3443/summary.json`.

The committed test entry point subsequently rebuilt the exact pinned diagnostic
ELF and repeated all five cases successfully on both routes. Its receipts are
under `.source/gpu-stage1/s41-ahb-padding/logs/`:

- S26: `gpu-proof-20261031-221732-25146/summary.json`.
- S41: `gpu-proof-20261031-221927-31218/summary.json`.

Both reported unchanged shared source/build/installed inputs and unchanged
live user sessions. WM and client processes had no fence interposer preload.

### Continuous resizing

The native-resolution observer reads the bottom 32 client rows and an adjacent
row about every 25 ms, using the actual TrueColor masks and excluding window
decorations. A line candidate requires a horizontal black run adjacent to gray.
It records geometry races separately. Readback timing and X11 pixmap samples
do not establish Android physical display timing.

Initial identical 12-second mixed-size drags, at 8 ms input steps:

| Observation | S26 | S41 |
|---|---:|---:|
| Samples during resizing | 478 | 479 |
| Black-line candidates | 178 | 0 |
| Candidates with matching before/after client and backing dimensions | 100 / 271 | 0 / 245 |
| Empty 500 ms Present COPY bins | 0 | 0 |
| Maximum completion-receive gap | 69 ms | 79 ms |

The S26 native crop shows two black rows across 534 of 539 client pixels near
the bottom, with a gray row immediately above. A subsequent S41 12-second run
had **0 line candidates and 0 all-black bottom blocks in 656 samples**, with
a maximum Present completion-receive gap of 70 ms.

The first S41 capture also contained 19 samples with an entirely black bottom
region. Those are a different predicate from the thin line and are retained in
the logs. Three follow-ups (12, 15 and 15 seconds of dragging) observed no such
block and no line candidate in 656, 772 and 762 total samples. The last probe
deferred all overlay queries until a black-block trigger, preserving the
original sampling requests before a trigger. No trigger occurred, so there
was no compositor comparison for the initial 19 samples. Their cause and
physical visibility remain unclassified; they are not counted as a proven
display artifact or silently treated as a successful visual check.

S41 also passed 200 x 1, 200 x 2, 400 x 1 and recovery to 500 x 500. Fixed-size
animation advanced after those sizes, and Escape closed the coalesced client
with exit status 0.

Initial continuous-test receipts under `.source/gpu-stage1/s41-bottom-edge/logs/`:

- S26: `formal-vkcube-coalesced-interactive-8ms-20261031-220550-22391`.
- S41 with thin-size and close checks: `formal-vkcube-coalesced-interactive-8ms-20261031-220650-26265`.
- S41 with a separate black-block/compositor diagnostic: `formal-vkcube-coalesced-interactive-8ms-20261031-221247-11159`.
- S41 15-second follow-up with thin-size and close checks: `formal-vkcube-coalesced-interactive-8ms-20261031-221429-16433`.
- S41 15-second follow-up with deferred overlay access: `formal-vkcube-coalesced-interactive-8ms-20261031-221800-26414`.

The final follow-up had 721 core Present COPY completions, no empty 500 ms
bins, a 70 ms maximum completion-receive gap and exit 0 after thin-size recovery.
This verifies progress in the X11 presentation path; it is not a measurement
of physical display FPS.

## Build, reproduce, and run

The builder uses the pinned existing S26 private build as its input. It copies
and patches only `wsi_common.c` into the S41 area, compiles one replacement
object, materializes the original thin archive into a private ordinary archive,
and relinks a separate wrapper. Source files, all thin-archive members, shared
link inputs, the S26 runtime and the protected installed-driver manifest are
checked for unchanged contents. The published S41 library hash is pinned.

```sh
cd /data/data/com.termux/files/home/alroot_wayland
python graphics/tools/build-s41-wrapper-padding.py
python graphics/tools/test-s41-ahb-padding.py --s26
python graphics/tools/test-s41-ahb-padding.py
python graphics/tools/run-s41-vkcube-visual.py
```

The visual runner requires the already-running verified S39 server and uses
the existing coalesced vkcube. It checks the selected ICD's exact library path
and SHA-256. It leaves the window open for manual resizing. An S39 desktop
requires no restart to run this new private client route.

The S40 Vulkan command remains the S26 comparison route:

```sh
python graphics/tools/run-s40-resize-visual.py vulkan
```

The S40 glxgears command and its selected library/client remain unchanged.

## Provenance

| Component | SHA-256 |
|---|---|
| S26 wrapper | `5fc1053bfeb549d31f0f748581472bb82d776d5e0ac08e0905b0fdc5fc7fbcf1` |
| Original `wsi_common.c` | `f132324c5f892c86e98a3cf4d68a752598455b49129a7ddc825c211ce3abf5d0` |
| S41 `wsi_common.c` | `dcffb2d95a8b493580856ae1ca0726d8a15996ad7f53530cfe7e85fa4af9af26` |
| S41 patch | `a077c1aae0df79daef60ab782a64490dc62cd8d5d54af38419b60281e4525a6e` |
| S41 wrapper | `21fdc1624cf35f957947e1e73813a6e3fbbcbb168eb18ce6bca2f96c0b03f11a` |
| S39 Xwayland | `c5f6c9a512f84e31f60ec52a9f10e689e0746f831049e57a4223ade2750f3899` |
| S39 coalesced vkcube | `0050e7126c8d096cd14a7e0b1026b7063f898e33eb7be5b80116991cee57c208` |
| S40 coalesced glxgears | `4f5444726d455d522ebaf37b17760febc028db4f6653dcd237517913c836e086` |
| Diagnostic cube source | `31663d8f2ea4aae81223199d243ce012200e96575a33f799560dfebaeb93c724` |
| Diagnostic cube executable | `976390a415f360f22922a09bd620c1b8594a37e77cae44dc6ed1a99692e6d0c6` |

The diagnostic's CPU reads are confined to its own test pixmap/window after
COPY completion and Idle. Production clients use the GPU-only padding copy.
The installed graphics libraries, Android APK, rootfs and Xwayland selection
are outside this change. Physical Android display acceptance remains the
user's manual observation.
