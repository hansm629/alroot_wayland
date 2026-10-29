/* Small vendor-neutral GLES -> AHardwareBuffer rendering probe.
 *
 * Uses Android's REAL libEGL/libGLESv2 and libnativewindow in a disposable
 * process, not Termux Mesa and not the user's active desktop. No pixels are
 * CPU-read. A successful GPU clear proves only client-side FBO viability,
 * NOT Wayland export, cross-process Vulkan import, or release synchronization.
 * Relevant for Mali, PowerVR, Adreno and Xclipse ANGLE-on-Vulkan alike.
 */
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef EGL_NATIVE_BUFFER_ANDROID
#define EGL_NATIVE_BUFFER_ANDROID 0x3140
#endif

#define LOAD(lib, name) ((typeof(&name))dlsym((lib), #name))

int main(void) {
    void *negl = dlopen("/system/lib64/libEGL.so", RTLD_NOW | RTLD_LOCAL);
    void *ngl = dlopen("/system/lib64/libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    void *nwin = dlopen("/system/lib64/libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
    if (!negl || !ngl || !nwin) {
        puts("ANDROID_AHB_EGL=LIBRARY_UNAVAILABLE");
        return 2;
    }
    PFNEGLGETDISPLAYPROC dpy_get = LOAD(negl, eglGetDisplay);
    PFNEGLINITIALIZEPROC init = LOAD(negl, eglInitialize);
    PFNEGLTERMINATEPROC term = LOAD(negl, eglTerminate);
    PFNEGLCHOOSECONFIGPROC choose = LOAD(negl, eglChooseConfig);
    PFNEGLBINDAPIPROC bind_api = LOAD(negl, eglBindAPI);
    PFNEGLCREATECONTEXTPROC context_create = LOAD(negl, eglCreateContext);
    PFNEGLCREATEPBUFFERSURFACEPROC surface_create = LOAD(negl, eglCreatePbufferSurface);
    PFNEGLMAKECURRENTPROC current = LOAD(negl, eglMakeCurrent);
    PFNEGLDESTROYCONTEXTPROC context_destroy = LOAD(negl, eglDestroyContext);
    PFNEGLDESTROYSURFACEPROC surface_destroy = LOAD(negl, eglDestroySurface);
    PFNEGLGETPROCADDRESSPROC proc = LOAD(negl, eglGetProcAddress);
    int (*allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **) =
        (int (*)(const AHardwareBuffer_Desc *, AHardwareBuffer **))
        dlsym(nwin, "AHardwareBuffer_allocate");
    void (*release)(AHardwareBuffer *) =
        (void (*)(AHardwareBuffer *))dlsym(nwin, "AHardwareBuffer_release");
    void (*gen_textures)(GLsizei, GLuint *) = LOAD(ngl, glGenTextures);
    void (*bind_texture)(GLenum, GLuint) = LOAD(ngl, glBindTexture);
    void (*tex_param)(GLenum, GLenum, GLint) = LOAD(ngl, glTexParameteri);
    void (*delete_textures)(GLsizei, const GLuint *) = LOAD(ngl, glDeleteTextures);
    void (*gen_fbos)(GLsizei, GLuint *) = LOAD(ngl, glGenFramebuffers);
    void (*bind_fbo)(GLenum, GLuint) = LOAD(ngl, glBindFramebuffer);
    void (*attach_fbo)(GLenum, GLenum, GLenum, GLuint, GLint) =
        LOAD(ngl, glFramebufferTexture2D);
    GLenum (*check_fbo)(GLenum) = LOAD(ngl, glCheckFramebufferStatus);
    GLenum (*error)(void) = LOAD(ngl, glGetError);
    void (*clear_color)(GLfloat, GLfloat, GLfloat, GLfloat) = LOAD(ngl, glClearColor);
    void (*clear)(GLbitfield) = LOAD(ngl, glClear);
    void (*flush)(void) = LOAD(ngl, glFlush);
    void (*delete_fbos)(GLsizei, const GLuint *) = LOAD(ngl, glDeleteFramebuffers);
    if (!dpy_get || !init || !term || !choose || !bind_api || !context_create ||
        !surface_create || !current || !context_destroy || !surface_destroy ||
        !proc || !allocate || !release || !gen_textures || !bind_texture ||
        !tex_param || !delete_textures || !gen_fbos || !bind_fbo || !attach_fbo ||
        !check_fbo || !error || !clear_color || !clear || !flush || !delete_fbos) {
        puts("ANDROID_AHB_EGL=REQUIRED_SYMBOL_MISSING");
        return 2;
    }
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC get_native =
        (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)proc("eglGetNativeClientBufferANDROID");
    PFNEGLCREATEIMAGEKHRPROC create_image =
        (PFNEGLCREATEIMAGEKHRPROC)proc("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC destroy_image =
        (PFNEGLDESTROYIMAGEKHRPROC)proc("eglDestroyImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)proc("glEGLImageTargetTexture2DOES");
    if (!get_native || !create_image || !destroy_image || !image_texture) {
        puts("ANDROID_AHB_EGL=IMAGE_BINDING_UNAVAILABLE");
        return 3;
    }

    int status = 3;
    EGLDisplay display = dpy_get(EGL_DEFAULT_DISPLAY);
    EGLint ma = 0, mi = 0;
    if (display == EGL_NO_DISPLAY || !init(display, &ma, &mi)) {
        puts("ANDROID_AHB_EGL=INIT_FAILED");
        return status;
    }
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLImageKHR img = EGL_NO_IMAGE_KHR;
    AHardwareBuffer *ahb = NULL;
    GLuint texture = 0, fbo = 0;
    EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLConfig config = NULL; EGLint count = 0;
    if (!bind_api(EGL_OPENGL_ES_API) ||
        !choose(display, attrs, &config, 1, &count) || count < 1) {
        puts("ANDROID_AHB_EGL=NO_GLES_CONFIG");
        goto done;
    }
    surf = surface_create(display, config, size);
    ctx = context_create(display, config, EGL_NO_CONTEXT, ctx_attrs);
    if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT ||
        !current(display, surf, surf, ctx)) {
        puts("ANDROID_AHB_EGL=NO_GLES_CONTEXT");
        goto done;
    }
    AHardwareBuffer_Desc desc = {0};
    desc.width = 64; desc.height = 64; desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    const char *requested_format=getenv("ALROOT_TEST_AHB_FORMAT");
    if (requested_format && strcmp(requested_format,"bgra8")==0)
        desc.format=5; /* Optional Android gralloc BGRA8, probe before use */
    printf("ANDROID_AHB_EGL_FORMAT=%s\n",desc.format==5?"BGRA8":"RGBA8");
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                 AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if (allocate(&desc, &ahb) || !ahb) {
        puts("ANDROID_AHB_EGL=AHB_ALLOC_FAILED");
        goto done;
    }
    EGLClientBuffer client = get_native(ahb);
    if (!client) {
        puts("ANDROID_AHB_EGL=NO_NATIVE_CLIENT_BUFFER");
        goto done;
    }
    img = create_image(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client, NULL);
    if (img == EGL_NO_IMAGE_KHR) {
        puts("ANDROID_AHB_EGL=IMAGE_IMPORT_FAILED");
        goto done;
    }
    gen_textures(1, &texture);
    bind_texture(GL_TEXTURE_2D, texture);
    tex_param(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    tex_param(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    image_texture(GL_TEXTURE_2D, img);
    if (error() != GL_NO_ERROR) {
        puts("ANDROID_AHB_EGL=GL_IMAGE_BIND_FAILED");
        goto done;
    }
    gen_fbos(1, &fbo);
    bind_fbo(GL_FRAMEBUFFER, fbo);
    attach_fbo(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (check_fbo(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        puts("ANDROID_AHB_EGL=NOT_RENDERABLE_AS_FBO");
        goto done;
    }
    clear_color(.2f, .4f, .8f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    flush();  /* No glFinish, no CPU pixel readback. */
    if (error() == GL_NO_ERROR) {
        puts("ANDROID_AHB_EGL=RENDER_SUBMITTED");
        puts("ANDROID_AHB_EGL_CPU_PIXEL_COPY=0");
        puts("ANDROID_AHB_EGL_WAYLAND_PRESENT=NOT_TESTED");
        status = 0;
    } else puts("ANDROID_AHB_EGL=GPU_RENDER_ERROR");

done:
    if (fbo) delete_fbos(1, &fbo);
    if (texture) delete_textures(1, &texture);
    if (img != EGL_NO_IMAGE_KHR) destroy_image(display, img);
    if (ahb) release(ahb);
    current(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (ctx != EGL_NO_CONTEXT) context_destroy(display, ctx);
    if (surf != EGL_NO_SURFACE) surface_destroy(display, surf);
    term(display);
    dlclose(nwin); dlclose(ngl); dlclose(negl);
    return status;
}
