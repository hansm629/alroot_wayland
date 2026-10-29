add_library(alrootwayland SHARED
        src/compositor.c
        src/vk_present.c
        src/effects_chain.c
        src/cardboard.c
        src/spatialpose.c
        src/vk_loader.c
        src/sc_layer.c
        src/ahb_swapchain.c
        src/alroot_gpu_buffer.c
        src/waylandcomp_jni.c
        src/gpu_clock_pin.c
        src/alroot_ext.c
        src/wl_dmabuf.c
        src/wl_clipboard.c
        src/wl_text_input.c
        src/wl_toplevel_icon.c
        src/wl_single_pixel.c
        src/wl_color_mgmt.c
        src/hdr_compose.c
        src/blend_pass.c
        generated/color-management-v1-protocol.c
        generated/xdg-shell-protocol.c
        generated/linux-dmabuf-v1-protocol.c
        generated/viewporter-protocol.c
        generated/banner-desktop-v1-protocol.c
        generated/presentation-time-protocol.c
        generated/pointer-constraints-unstable-v1-protocol.c
        generated/relative-pointer-unstable-v1-protocol.c
        generated/wlr-data-control-unstable-v1-protocol.c
        generated/text-input-unstable-v3-protocol.c
        generated/xdg-toplevel-icon-v1-protocol.c
        generated/single-pixel-buffer-v1-protocol.c
        generated/banner-ahb-v1-protocol.c
        generated/alroot-gpu-buffer-v1-protocol.c
        src/framegen_bridge.c
        src/framegen_jni.c
)
target_compile_options(alrootwayland PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers>)
target_include_directories(alrootwayland PRIVATE
        generated
        prebuilt/include
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/../shaders
)

set(ALROOT_FRAMEGEN_LSFG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../framegen/lsfg)
set(ALROOT_FRAMEGEN_DDFG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../framegen/ddfg)
if (TARGET dxbc AND EXISTS ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_engine.cpp
        AND EXISTS ${ALROOT_FRAMEGEN_DDFG_DIR}/ddfg_engine.cpp)
    message(STATUS "alrootwayland: frame-generation engines built in (LSFG Native, DD-FG Native)")
    target_sources(alrootwayland PRIVATE
            src/framegen_engine.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_probe.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_dll.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_dxbc.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_vkd.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_common.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_shaders.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_mipmaps.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_alpha.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_beta.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_gamma.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_delta.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_generate.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_chain.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_pacer.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_engine.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_governor.cpp
            ${ALROOT_FRAMEGEN_LSFG_DIR}/lsfg_jni.cpp
            ${ALROOT_FRAMEGEN_DDFG_DIR}/framegen.cpp
            ${ALROOT_FRAMEGEN_DDFG_DIR}/ddfg_vkd.cpp
            ${ALROOT_FRAMEGEN_DDFG_DIR}/ddfg_engine.cpp
    )
    target_include_directories(alrootwayland PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_SOURCE_DIR}/../framegen
    )
    target_compile_options(alrootwayland PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-std=c++17 -fvisibility=hidden>)
    target_compile_definitions(alrootwayland PRIVATE
            DDFG_UPSTREAM="${DDFG_UPSTREAM}"
            DDFG_CHAIN_HASH="${DDFG_CHAIN_HASH}"
    )
    target_link_libraries(alrootwayland dxbc atomic)
else()
    message(STATUS "alrootwayland: frame-generation engines NOT built in (standalone build): stub")
    target_sources(alrootwayland PRIVATE src/framegen_engine_stub.c)
endif()
add_library(wayland-server SHARED IMPORTED)
set_target_properties(wayland-server PROPERTIES IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/lib/libwayland-server.so)
target_link_libraries(alrootwayland
        wayland-server
        adrenotools
        dl
        log
        android
        nativewindow
)
