# S39 private vkcube 1.4.365 build inputs

License: Apache-2.0. The upstream source's original copyright and license notice
is retained. The local coalescing change and builder are also Apache-2.0.

Official source archive:

<https://github.com/KhronosGroup/Vulkan-Tools/archive/refs/tags/v1.4.365.tar.gz>

SHA256: `c55db29b4b2a4d222053ddcc7f925b8cad7f16f5640a05d4ee16d44a9dc02aac`.

Patches 0001 through 0003 preserve the changes from the Termux vulkan-tools 1.4.365
package, commit `a882692cbdfb101ab2b677b5d9f7623380ede5f3`:

<https://github.com/termux/termux-packages/tree/a882692cbdfb101ab2b677b5d9f7623380ede5f3/packages/vulkan-tools>

Empty patch context-line prefixes are normalized for repository whitespace checks;
the resulting patched source hashes remain pinned.

Patch 0004 is the local application-specific change. It moves the XCB
ConfigureNotify-triggered resize out of the event handler and performs one resize
using the latest received dimensions after the pending events have been handled.
It does not change a Vulkan ICD, compositor, surface capability, frame pacing, or
another application. Acquire/present recovery remains upstream behavior.

`graphics/tools/build-s39-vkcube.py` pins the archive, all four patches, pristine
cube.c, Termux-patched cube.c, and coalesced cube.c. It builds baseline and candidate
with the same configuration in these owned directories:

- `.source/gpu-stage1/s39-live-resize/vkcube/source`
- `.source/gpu-stage1/s39-live-resize/vkcube/build`

Outputs are private `runtime/vkcube.baseline` and `runtime/vkcube.coalesced` under
S39. The installed package binary remains unchanged. `--activate` additionally
copies the candidate atomically to S39's private `runtime/vkcube`; the default
only builds the two variants. Replacement is refused while the relevant private
executable is running. The script performs no global installation.

The build manifest is S39's `vkcube-build-manifest.json`. Recorded binary hashes
are compared with the initial Clang 21.1.8 build as reproducibility evidence;
toolchain-dependent binary differences are recorded rather than silently assumed
equivalent. Build success is not a visual or Present regression result.

Use the private baseline and coalesced variants for a controlled comparison:
both use Release, XCB plus Xlib, and no Wayland/display WSI. Xlib is enabled because
this upstream build needs its Display type even when exercising `--wsi xcb`.
