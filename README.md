# Alroot Wayland — developer helper (P01)

Independent Android display helper based on pinned DroidDeck native compositor.
Target: S26U, Termux Bionic XFCE4 via rootful Xwayland; nested Labwc remains an
explicit legacy profile. The user-authorized S42 default uses installed Vulkan
Wrapper + Mesa 27 Zink. See [the S42 package/default policy and validation
record](graphics/docs/gpu-s42-installed-defaults.md) for deployment status.
This is a development build, not a production-ready desktop or zero-copy path.

Wayland outputs, JNI symbols and runtime log identifiers are Alroot-specific;
upstream GPLv3 provenance is still credited in LICENSING.md and upstream.lock.

See docs/BUILD.md, docs/RUN.md, docs/STATUS.md, docs/SOURCES.md, LICENSING.md.
