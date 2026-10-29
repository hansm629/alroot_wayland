# Host Stage-1 GPU-copy patch order

Base: alroot_wayland P20 (b9f4162), retaining the version-2
banner_ahb_v1 interface in the current installed APK.

Apply **only to a disposable build checkout**, in order:

1. 0001-ahb-to-vkimage-gpu-copy.patch: client-attached AHardwareBuffer
   gets the native host Vulkan AHB import **first**. Standard DMA-BUF
   import remains the fallback, and is unchanged for clients without
   AHB. Every frame still undergoes Vulkan GPU composition/copy.
2. 0002-v3-gpucopy-capability-independent-of-layers.patch: advertise
   banner_ahb_v1 version 3 when either AHB VkImage import or a direct
   display layer is available. Separately report capability bit 0 for
   AHB GPU-copy and bit 1 for direct layers.

Legacy v1/v2 clients keep their existing mode event semantics, which
control **direct layers only**. Version-3 clients may negotiate GPU-copy
when direct layers are off, but must independently validate AHB image
allocation, synchronization, formats, and final host presentation.

The existing ahb_swapchain_advertised() function retains its historical
direct-layer semantics for HDR gating. A GPU-copy-only host must NOT
accidentally advertise HDR zero-copy.

Run graphics/tools/test-host-overlay.sh to apply both patches to a
disposable P20 archive, check protocol structure against wayland-scanner,
and compile C sources with Android API 35. Test-ahb-protocol.sh exercises
v2 backward compatibility and the v3 copy-only negotiation event.
Neither test changes the installed APK, driver, or live session.
