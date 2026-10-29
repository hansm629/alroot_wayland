/* Query the REAL Android EGL/GLES loader, not Termux's Mesa libEGL.so.
 * This process is read-only and temporary; it creates a 1x1 pbuffer only.
 * In particular Xclipse's GLES may be ANGLE over Vulkan: do not conflate
 * this with Mesa KGSL/Zink EGL used by Termux/Linux clients.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

static int has(const char *all, const char *name) {
    if (!all || !name) return 0;
    size_t n = strlen(name);
    for (const char *p = all; (p = strstr(p, name)); p += n)
        if ((p == all || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0))
            return 1;
    return 0;
}

#define GET(lib, symbol) ((typeof(&symbol))dlsym((lib), #symbol))
int main(void) {
    void *egl = dlopen("/system/lib64/libEGL.so", RTLD_NOW | RTLD_LOCAL);
    void *gles = dlopen("/system/lib64/libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    if (!egl) {
        printf("ANDROID_EGL=UNAVAILABLE\nERROR=%s\n", dlerror());
        if (gles) dlclose(gles);
        return 2;
    }
    PFNEGLGETDISPLAYPROC pGetDisplay = GET(egl, eglGetDisplay);
    PFNEGLINITIALIZEPROC pInitialize = GET(egl, eglInitialize);
    PFNEGLQUERYSTRINGPROC pQueryString = GET(egl, eglQueryString);
    PFNEGLTERMINATEPROC pTerminate = GET(egl, eglTerminate);
    PFNEGLCHOOSECONFIGPROC pChooseConfig = GET(egl, eglChooseConfig);
    PFNEGLBINDAPIPROC pBindAPI = GET(egl, eglBindAPI);
    PFNEGLCREATEPBUFFERSURFACEPROC pPbuffer = GET(egl, eglCreatePbufferSurface);
    PFNEGLCREATECONTEXTPROC pContext = GET(egl, eglCreateContext);
    PFNEGLMAKECURRENTPROC pMakeCurrent = GET(egl, eglMakeCurrent);
    PFNEGLDESTROYSURFACEPROC pDestroySurface = GET(egl, eglDestroySurface);
    PFNEGLDESTROYCONTEXTPROC pDestroyContext = GET(egl, eglDestroyContext);
    const GLubyte *(*pGlGetString)(GLenum) =
        gles ? (const GLubyte *(*)(GLenum))dlsym(gles, "glGetString") : NULL;
    if (!pGetDisplay || !pInitialize || !pQueryString || !pTerminate ||
        !pChooseConfig || !pBindAPI || !pPbuffer || !pContext || !pMakeCurrent ||
        !pDestroySurface || !pDestroyContext) {
        puts("ANDROID_EGL=SYMBOLS_MISSING");
        if (gles) dlclose(gles);
        dlclose(egl);
        return 2;
    }
    EGLDisplay display = pGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !pInitialize(display, &major, &minor)) {
        puts("ANDROID_EGL=INITIALIZE_FAILED");
        if (gles) dlclose(gles);
        dlclose(egl);
        return 2;
    }
    const char *ext = pQueryString(display, EGL_EXTENSIONS);
    printf("ANDROID_EGL=OK\nANDROID_EGL_VERSION=%d.%d\nANDROID_EGL_VENDOR=%s\n",
           (int)major, (int)minor, pQueryString(display, EGL_VENDOR));
    printf("ANDROID_EGL_NATIVE_BUFFER=%d\n",
           has(ext, "EGL_ANDROID_image_native_buffer"));
    printf("ANDROID_EGL_NATIVE_FENCE=%d\n",
           has(ext, "EGL_ANDROID_native_fence_sync"));
    printf("ANDROID_EGL_DMABUF_IMPORT=%d\n",
           has(ext, "EGL_EXT_image_dma_buf_import"));
    printf("ANDROID_EGL_DMABUF_EXPORT=%d\n",
           has(ext, "EGL_MESA_image_dma_buf_export"));
    printf("ANDROID_EGL_IMAGE_BASE=%d\n",
           has(ext, "EGL_KHR_image_base"));
    EGLint cfg_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLint attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLConfig config = NULL; EGLint count = 0;
    if (pGlGetString && pBindAPI(EGL_OPENGL_ES_API) &&
        pChooseConfig(display, cfg_attrs, &config, 1, &count) && count) {
        EGLSurface surface = pPbuffer(display, config, attrs);
        EGLContext context = pContext(display, config, EGL_NO_CONTEXT, context_attrs);
        if (surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
            pMakeCurrent(display, surface, surface, context)) {
            const GLubyte *renderer = pGlGetString(GL_RENDERER);
            const GLubyte *vendor = pGlGetString(GL_VENDOR);
            printf("ANDROID_GLES=OK\nANDROID_GLES_VENDOR=%s\nANDROID_GLES_RENDERER=%s\n",
                   vendor ? (const char *)vendor : "UNKNOWN",
                   renderer ? (const char *)renderer : "UNKNOWN");
            /* ANGLE/Vulkan is a GLES implementation. Do not treat it as a
             * verified GLAMOR exporter solely from this renderer string. */
            pMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        } else puts("ANDROID_GLES=NO_CONTEXT");
        if (context != EGL_NO_CONTEXT) pDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) pDestroySurface(display, surface);
    } else puts("ANDROID_GLES=NO_CONFIG");
    pTerminate(display);
    if (gles) dlclose(gles);
    dlclose(egl);
    return 0;
}
