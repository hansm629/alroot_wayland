# Source provenance

Pinned upstream: Droid-Deck/DroidDeck main SHA
0f839bef5d5cf109af614d0ff15844c95a8e16d0.

Copied sources: native/waylandcomp (including Vulkan, AHB, Wayland SHM,
protocols, clipboard and JNI), native/adrenotools, native/shaders, and upstream
WaylandCompositor.java. Imported libffi and libwayland-server are in Android
jniLibs with hashes in upstream.lock. The Android front-end/service and stop
request are Alroot-specific modifications, not a compositor rewrite.

Licenses in LICENSES/ and third-party notices retained. Corresponding source
of imported third-party binary libraries requires audit before public binary
redistribution.
