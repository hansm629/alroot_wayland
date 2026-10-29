/* Read-only EGL capability probe. Runs with any EGL vendor (Mesa, ANGLE,
 * PowerVR, Mali etc). It does not allocate or export a wl_buffer, so its
 * results are an INITIAL FILTER and never a GPU-presentation PASS. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_PLATFORM_WAYLAND_EXT
#define EGL_PLATFORM_WAYLAND_EXT 0x31D8
#endif

static int extension_present(const char *all, const char *ext) {
    size_t len = strlen(ext);
    if (!all || !ext || !*ext) return 0;
    for (const char *p = all; (p = strstr(p, ext)); p += len) {
        if ((p == all || p[-1] == ' ') && (p[len] == '\0' || p[len] == ' ')) return 1;
    }
    return 0;
}

static void capability(const char *key, const char *exts, const char *name) {
    printf("%s=%d\n", key, extension_present(exts, name));
}

int main(int argc, char **argv) {
    const int wayland = argc == 2 && !strcmp(argv[1], "wayland");
    if (argc != 2 || (strcmp(argv[1], "wayland") && strcmp(argv[1], "surfaceless"))) {
        fprintf(stderr, "usage: %s wayland|surfaceless\n", argv[0]);
        return 2;
    }
    struct wl_display *wl = NULL;
    if (wayland && !(wl = wl_display_connect(NULL))) {
        fputs("EGL_PROBE=NO_WAYLAND_CONNECTION\n", stderr);
        return 3;
    }
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (get_platform)
        dpy = get_platform(wayland ? EGL_PLATFORM_WAYLAND_EXT : EGL_PLATFORM_SURFACELESS_MESA,
                           wayland ? (void *)wl : EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY) {
        fputs("EGL_PROBE=NO_PLATFORM_DISPLAY\n", stderr);
        if (wl) wl_display_disconnect(wl);
        return 3;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "EGL_PROBE=INITIALIZE_FAILED error=0x%x\n", eglGetError());
        if (wl) wl_display_disconnect(wl);
        return 3;
    }
    const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("EGL_PROBE=OK\nEGL_PLATFORM=%s\nEGL_VERSION=%d.%d\nEGL_VENDOR=%s\n",
           argv[1], (int)major, (int)minor, eglQueryString(dpy, EGL_VENDOR));
    capability("EGL_EXT_IMAGE_DMA_BUF_IMPORT", exts, "EGL_EXT_image_dma_buf_import");
    capability("EGL_EXT_IMAGE_DMA_BUF_MODIFIERS", exts, "EGL_EXT_image_dma_buf_import_modifiers");
    capability("EGL_MESA_IMAGE_DMA_BUF_EXPORT", exts, "EGL_MESA_image_dma_buf_export");
    capability("EGL_KHR_IMAGE_BASE", exts, "EGL_KHR_image_base");
    capability("EGL_ANDROID_IMAGE_NATIVE_BUFFER", exts, "EGL_ANDROID_image_native_buffer");
    capability("EGL_ANDROID_NATIVE_FENCE_SYNC", exts, "EGL_ANDROID_native_fence_sync");
    capability("EGL_KHR_SURFACELESS_CONTEXT", exts, "EGL_KHR_surfaceless_context");
    const int export_function_present =
        eglGetProcAddress("eglExportDMABUFImageMESA") != NULL &&
        eglGetProcAddress("eglExportDMABUFImageQueryMESA") != NULL;
    printf("EGL_EXPORT_DMABUF_FUNCTION=%d\n", export_function_present);
    printf("EGL_DMA_BUF_EXPORT_CANDIDATE=%d\n",
           export_function_present &&
           extension_present(exts, "EGL_MESA_image_dma_buf_export"));

    if (eglBindAPI(EGL_OPENGL_ES_API)) {
        EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
        EGLConfig config = NULL;
        EGLint configs = 0;
        if (eglChooseConfig(dpy, attrs, &config, 1, &configs) && configs > 0) {
            EGLint pbuf_attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
            EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
            EGLSurface surface = eglCreatePbufferSurface(dpy, config, pbuf_attrs);
            EGLContext context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, context_attrs);
            if (surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
                eglMakeCurrent(dpy, surface, surface, context)) {
                printf("EGL_GLES_CONTEXT=1\nGLES_VENDOR=%s\nGLES_RENDERER=%s\n",
                       glGetString(GL_VENDOR), glGetString(GL_RENDERER));
                eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            } else {
                puts("EGL_GLES_CONTEXT=0");
            }
            if (context != EGL_NO_CONTEXT) eglDestroyContext(dpy, context);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(dpy, surface);
        } else puts("EGL_GLES_CONTEXT=0");
    } else puts("EGL_GLES_CONTEXT=0");
    eglTerminate(dpy);
    if (wl) wl_display_disconnect(wl);
    return 0;
}
