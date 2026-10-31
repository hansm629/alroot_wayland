# GPU-S44 — installed submission overhead improvements

## Result

**ACTIVE_VERIFIED**, 2026-11-01 (Asia/Seoul), S26 Ultra / Termux Bionic.

The accepted S42 Wrapper/Mesa sources were rebuilt with measured submission
changes and installed through their existing owner packages:

| Package | Installed version |
| --- | --- |
| vulkan-wrapper-android | `26.3.0+alroot.s44.1` |
| mesa | `27.0.0~devel+alroot.s44.1` |
| mesa-dev | `27.0.0~devel+alroot.s44.1` |

The [S43 vkcube correction](gpu-s43-vkcube-default-wsi.md) remains installed
as `vulkan-tools 1.4.365+alroot.s42.2`. The current desktop was not restarted:
Xwayland PID 21338 and XFCE PID 21462 retained their start identities.

Both optimizations are compiled defaults. New application processes use
them without additional enabling variables, including applications started
from an existing terminal. Already-running applications retain their loaded
libraries until restarted. Normal execution produces no S44 profiling output.

[Machine-readable results](gpu-s44-submit-overhead-results.json) contain
package identities, source/patch hashes, every timed sample, regression
results, installed default diagnostics, rollback identities and raw-receipt
hashes. Raw receipts remain under
`.source/gpu-stage1/s44-sync-overhead` on the device.

## What changed

### Wrapper submit translation

[0006](../patches/mesa/wrapper/0006-s44-submit-stack.patch) introduced
aggregate diagnostics and a bounded stack path;
[0008](../patches/mesa/wrapper/0008-wrapper-small-multi-submit.patch)
extends that path to the actual multi-batch Zink submissions and enables it
by default.

The final path supports at most **five submit headers and eight total
command buffers**, for both `vkQueueSubmit` and `vkQueueSubmit2`. Each
nonempty batch receives its own slice of a fixed command array, live until
the one vendor call returns. Complete headers and command-submit records are
copied; only wrapped command-buffer handles are translated. Wait/signal
arrays, stage masks, extension pointers, timeline values, batch ordering,
fence arguments and returned errors are preserved.

Subtraction from a bounded budget prevents count overflow from selecting the
stack path. Larger submissions retain the existing heap fallback. A null
fence avoids a mutex-protected lookup that would return null. Zero-submit
calls, including fence signaling, are forwarded without forming a zero-sized
VLA.

The profiled Vulkan XCB, GLX, native EGL and native Vulkan workloads all used
this bounded path and reported **zero command-array heap allocations**.
This is a claim about these Wrapper allocations, not all driver/application
allocation or all possible workloads.

### Zink empty submit header

[Zink 0001](../patches/mesa/zink/0001-s44-submit-compaction-profile.patch)
removes an empty FD-wait header when an acquire-wait header precedes it.
Removal requires zero waits, commands and signals, and a null extension
chain. The acquire header is copied in full into the empty slot; later
command/signal headers and their timeline extension chains stay in place.
The existing queue call, its fence and retry handling remain unchanged.
[0002](../patches/mesa/zink/0002-zink-submit-default-on.patch) enables the
change by default.

For the observed GLX frame submission, the number of `VkSubmitInfo`
headers changes from **four to three: 25% fewer headers**. The number of
`vkQueueSubmit` calls and GPU rendering commands is not reduced by 25%.
Native EGL had no eligible empty FD-wait header; its benefit here is the
Wrapper allocation/lookup change.

The submission fields and ordering constraints are documented in the
[Vulkan VkSubmitInfo reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubmitInfo.html)
and [vkQueueSubmit reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html).
The change preserves nonempty synchronization operations.

## CPU waits: findings and retained guarantees

The investigation separated the active paths instead of assuming every
source-level wait was active:

| Path | Observed state / consequence |
| --- | --- |
| Common WSI `forcesync` | Disabled in the actual desktop and measured swapchains |
| X11 early fence workaround | Not active for the observed Xwayland connection |
| Zink implicit-sync workaround | Disabled for the forwarded Qualcomm proprietary driver ID |
| DRI previous-frame throttle | Active; timeline waits remain enabled |
| Native Vulkan Alroot AHB readiness | Active; required producer-completion wait remains |
| Native EGL Wayland | Zink rendering without a Vulkan WSI swapchain in this probe |

In the final staged native Vulkan probe, 1,200 frames accumulated about
**481 ms** in the AHB-ready fence wait. An earlier diagnostic accumulated
about 442 ms there and 3.06 seconds in acquisition/backpressure. These are
process-local diagnostic totals, not additive GPU time or a proven before/
after improvement. Backend-present timing includes the AHB wait; those
nested timers must not be summed.

The installed host's `alroot_gpu_buffer_v1` v2 transport does not carry an
acquire sync-file for this native AHB path. Removing the client readiness
wait alone would let the consumer access an unready buffer. No host APK,
protocol, producer/consumer ownership policy, image-copy path or zero-copy
implementation changes in S44.

**Mandatory CPU fence waiting itself was not shown to decrease.** This
stage removes repeated submission work and supplies attributable wait
measurements. A future GPU-wait handoff must also handle discarded frames,
buffer release, resize/destruction, import failure and source visibility.
A lower present-side wait alone would not prove improvement if the delay
simply moved into acquire or host composition.

## Timing results and limits

All timed A/B runs use the same final private binaries, 800×600 output,
the foreground cpuset, and profiling disabled. Vulkan WSI routes request
mailbox presentation; the native EGL probe uses swap interval 0. A means
both feature switches explicitly set to 0; B means both set to 1. The
default-on state was separately confirmed with both switches absent.

Five cases ran in A-B-B-A order. Vulkan light/heavy also ran in B-A-A-B order
to investigate the initial apparent heavy-case loss: **28 unprofiled runs**.
vkmark and glmark2 scenes lasted four seconds; the native EGL clear probe
completed 6,000 frames per timing run. Vulkan heavy used
`effect2d:kernel=blur:background-resolution=1920x1080`; GLX heavy used
`terrain`.

| Workload | Baseline mean FPS | Candidate mean FPS | Descriptive difference | Samples per variant |
| --- | ---: | ---: | ---: | ---: |
| Vulkan XCB vertex | 3336.50 | 3275.25 | -1.84% | 4 |
| Vulkan XCB effect2d | 1891.50 | 1996.50 | +5.55% | 4 |
| GLX build/VBO | 1928.50 | 2006.50 | +4.04% | 2 |
| GLX terrain | 468.50 | 493.00 | +5.23% | 2 |
| Native EGL Wayland clear | 1172.65 | 1168.02 | -0.40% | 2 |

These are descriptive sample averages, **not established improvement
percentages**. The Vulkan-heavy contrast changed from -9.96% in A-B-B-A to
+22.09% in B-A-A-B, with the first heavy run highest in either order.
Vulkan-light's small negative observation remains possible within the
short-run variability. No repeatable material regression was demonstrated;
a fixed FPS gain is not claimed.

GPU-frequency and useful thermal telemetry were unavailable at the accessible
interfaces. Run-order drift was observed, but is not attributed to a measured
thermal or frequency cause. CPU resource totals cover setup and teardown;
they do not establish precise CPU time per rendered frame or fence-wait
savings. This work did not benchmark SuperTuxKart or Android physical display
FPS.

Installed vkmark lacks its Wayland plugin. Its generic help text was not
treated as runtime coverage: native Vulkan attribution instead uses finite
installed vkcube with explicit Wayland selection, and EGL uses the separate
finite swap probe. Native EGL's zero WSI-chain count is explicitly valid.

## Validation

The [S44 session adapter](../tools/test-s44-graphics-session.py) preserves the
S42/S40 gates and uses the S43 XRes-owned close helper. Both **staged** and
**installed** runs passed for vkcube and glxgears:

- Six seconds of continuous resizing, eight-millisecond input steps.
- No zero-COPY-completion bins in the measured resize core.
- 200×1, 200×2 and 400×1 sizes, followed by 500×500 recovery.
- Fixed-size image advancement.
- Plain vkcube with both transports reachable chooses XCB in the X11 session.
- vkcube is managed/decorated, accepts the verified WM close request, and
  exits with code 0. glxgears retains its supported Escape exit.
- Selected library maps/hashes match the candidate, then canonical installed
  files. Client and WM runtime preloads are absent.
- Live desktop identities and unrelated installed files remain unchanged
  during each owned disposable-session test.

The COPY observer does not prove continuous rotation or pixel-perfect
appearance during resizing, nor physical Android FPS. That limitation is
preserved in every receipt; fixed-size image advancement is checked
separately.

Installed GLX, native EGL and native Vulkan diagnostic runs also passed with
normal canonical lookup, no blanket `LD_LIBRARY_PATH`, and both candidate
switches unset. Profiles prove Wrapper `enabled=1` and Zink
`compact_submits=1` where Zink is used.

[Source-extracted mocked dispatch tests](../tests/test-s44-wrapper-submit.py)
passed 102 cases under ASan/UBSan, covering both submit APIs, zero/empty/multi
batches, limits and overflow rejection, extension/command metadata, live array
slices, caller immutability, null/valid fences, fallback allocation ownership
and native errors. LeakSanitizer was disabled because this sandbox prevents
its task inspection; explicit allocation/free assertions remain active.
Policy fixtures passed 22 checks, and existing terminal fixtures passed eight.

## Packages and runtime identity

| Artifact | SHA-256 |
| --- | --- |
| Wrapper .deb | `93b1856f38c3a62aac3342a66a03a187b44ede0647fc8c8b90f7a9b31c7ea5c9` |
| Mesa .deb | `8c16c184483ffc15c6559861aa82d5341beffa9facfab0a7ccce1a837d46bc62` |
| mesa-dev .deb | `aac672443800d5a0686023986f02c57306a43440a63730f220eff88d98792474` |
| Installed Wrapper | `021a695932c881f8def8b5aadc65771ca5f812e6d7caf7055dc9db828055a9c0` |
| Installed Gallium | `2a9dadf8f1719ba713d7409caba92ca08dd47265b308c259e61775951d8f91ac` |
| Installed GLX Mesa | `966f15b54e9c389ee9b5671012abc610dcc9db689483193cc81daef7c4e5339c` |
| Installed EGL Mesa | `7fe733b59062aefa486b010c5f6ef462fcf6a0cf04b958c9d8df388aa47de929` |

The [packager](../tools/package-s44-graphics-debs.py) starts with exact S42
owner archives and retains them for rollback. It preserves file sets,
symlinks, directory/file modes, control scripts, non-ELF payload and canonical
ICD bytes. Measured, already-stripped stage bytes are not stripped again.
The package versions, sizes/checksums and owned provenance are updated.
mesa-dev's payload is unchanged.

Wrapper changes its one library and provenance. Mesa changes the rebuilt
EGL/GLX/Gallium/GBM/DRIL ELF files and its owned provenance. Mesa's historical
`share/doc/mesa/alroot-s42-build.json` path remains owned by Mesa; its content
now identifies the S44 version, hashes, defaults and source/evidence chain.

The original eight-file protected snapshot is retained as historical
evidence. **Five authorized target entries change; three non-target entries
match**: Freedreno/Turnip ICD, lavapipe ICD and the system
`$PREFIX/bin/Xwayland`. The S39 Alroot Xwayland binary remains separate and
unchanged. Other protected packages, vkcube and glxgears remain unchanged.
The source changes are confined to Wrapper/WSI instrumentation and Zink;
the shared Mesa rebuild also supplies the existing KGSL server renderer,
which passed the installed regression.

`dpkg --audit`, target-package verification and the real awstart preflight
passed after installation. The policy accepts coherent S42/S42 or S44/S44
Wrapper/Mesa pairs, validates S44 compiled-default/profile-off provenance,
and rejects mixed pairs before stopping a desktop.

## Reproduction and operational controls

- [Build/stage recipe](../tools/build-s44-graphics-stage.py): independent
  accepted S42 sources plus pinned patches and configure options.
  `--action recipe` is the default; `--action verify` checks current source
  and options without rebuilding. Future builds can use a named revision.
  Source/options reproducibility is supported; byte-identical rebuilds are
  not promised. The original S44 configure-time SOURCE_DATE_EPOCH was not
  recorded.
- [EGL probe generator](../tools/build-s44-egl-probe.py): recreates the exact
  finite 800×600 interval-zero probe without modifying the original S12 test.
- [Benchmark/diagnostic runner](../tools/test-s44-driver-overhead.py): records
  explicit runtime identities, flags, requested mode, CPU usage, receipts and
  normal teardown. Its `default` variant leaves optimization switches unset.
- Measurement-runner SHA was
  `aa9aafc43b6f8309703e4b82889e6255cade1f748d39bd348c545c2a633e7c63`.
  The final installed-prefix correction has SHA
  `249f1fe73fcdc8ce04c506b78a7ebde34bd8ba3d4f2106533cffdd0d3760c1dc`;
  private A/B behavior is unchanged.

For an explicitly requested per-command comparison:

```sh
S44_WRAPPER_SUBMIT_STACK=0 S44_ZINK_COMPACT_SUBMITS=0 vkcube
```

Each switch is read at process/screen initialization. This selects the
baseline submission behavior inside the new binaries, not the old package
bytes. Aggregate diagnostics are separately opt-in through
`S44_WRAPPER_PROFILE=1`, `S44_WSI_PROFILE=1` and
`S44_ZINK_SUBMIT_STATS=1`; do not enable them in timing runs.

Exact S42 rollback archives are retained in
`.source/gpu-stage1/s44-sync-overhead/rollback`. Restore the three owner
packages together; the updated policy continues to accept that coherent
baseline. S43 vulkan-tools remains installed through either graphics pair.
