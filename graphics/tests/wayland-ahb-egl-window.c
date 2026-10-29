/* Alroot Stage-1 Wayland window: Android EGL -> RGBA8 AHB -> wl_buffer.
 *
 * A vendor-independent diagnostic client using public NDK AHB transfer.
 * No DRM node, private native_handle structure, KGSL call or CPU pixel copy.
 * Uses the REAL Android EGL/GLES implementation, including ANGLE on Xclipse.
 * The initial GPU finish is a correctness fence for Stage 1, NOT the
 * subsequent low-overhead explicit-sync optimization.
 */
#define _GNU_SOURCE
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "alroot-gpu-buffer-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define API(lib, name) ((typeof(&name))dlsym((lib), #name))
#ifndef EGL_NATIVE_BUFFER_ANDROID
#define EGL_NATIVE_BUFFER_ANDROID 0x3140
#endif

struct demo {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm;
    struct alroot_gpu_buffer_v1 *manager;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    int configured, closed, released;
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t id,
                       const char *iface, uint32_t ver) {
    struct demo *d = data;
    if (!strcmp(iface, wl_compositor_interface.name) && !d->compositor)
        d->compositor = wl_registry_bind(reg, id, &wl_compositor_interface,
                                         ver < 4 ? ver : 4);
    else if (!strcmp(iface, xdg_wm_base_interface.name) && !d->wm)
        d->wm = wl_registry_bind(reg, id, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, alroot_gpu_buffer_v1_interface.name) && !d->manager)
        d->manager = wl_registry_bind(reg, id, &alroot_gpu_buffer_v1_interface, 1);
}
static void reg_removed(void *data, struct wl_registry *reg, uint32_t id) {
    (void)data; (void)reg; (void)id;
}
static const struct wl_registry_listener registry_listener =
    { .global=reg_global, .global_remove=reg_removed };
static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_listener = { .ping=wm_ping };
static void xdg_configure(void *data, struct xdg_surface *surf, uint32_t serial) {
    struct demo *d=data;
    xdg_surface_ack_configure(surf, serial);
    d->configured=1;
}
static const struct xdg_surface_listener xdg_listener = { .configure=xdg_configure };
static void top_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
                          struct wl_array *states) {
    (void)data; (void)t; (void)w; (void)h; (void)states;
}
static void top_close(void *data, struct xdg_toplevel *t) {
    (void)t;
    ((struct demo *)data)->closed=1;
}
static const struct xdg_toplevel_listener top_listener =
    { .configure=top_configure, .close=top_close };
static void buffer_release(void *data, struct wl_buffer *buffer) {
    (void)buffer;
    ((struct demo *)data)->released++;
}
static const struct wl_buffer_listener buffer_listener = { .release=buffer_release };

static void *resolve(void *lib, const char *name) {
    void *p=dlsym(lib,name);
    if (!p) fprintf(stderr,"ANDROID_EGL_SYMBOL_MISSING=%s\n",name);
    return p;
}

static int render_and_transfer(struct demo *d, uint32_t width, uint32_t height) {
    int result=-1;
    void *egl=dlopen("/system/lib64/libEGL.so", RTLD_NOW|RTLD_LOCAL);
    void *gles=dlopen("/system/lib64/libGLESv2.so", RTLD_NOW|RTLD_LOCAL);
    void *nwin=dlopen("/system/lib64/libnativewindow.so", RTLD_NOW|RTLD_LOCAL);
    if (!egl || !gles || !nwin) { puts("AHB_EGL_WINDOW=LIBRARY_UNAVAILABLE"); return -1; }
    PFNEGLGETDISPLAYPROC display_get=API(egl,eglGetDisplay);
    PFNEGLINITIALIZEPROC initialize=API(egl,eglInitialize);
    PFNEGLCHOOSECONFIGPROC choose=API(egl,eglChooseConfig);
    PFNEGLBINDAPIPROC bind_api=API(egl,eglBindAPI);
    PFNEGLCREATEPBUFFERSURFACEPROC make_pbuffer=API(egl,eglCreatePbufferSurface);
    PFNEGLCREATECONTEXTPROC make_context=API(egl,eglCreateContext);
    PFNEGLMAKECURRENTPROC current=API(egl,eglMakeCurrent);
    PFNEGLGETPROCADDRESSPROC proc=API(egl,eglGetProcAddress);
    PFNEGLDESTROYIMAGEKHRPROC release_image=(PFNEGLDESTROYIMAGEKHRPROC)resolve(egl,"eglDestroyImageKHR");
    PFNEGLCREATEIMAGEKHRPROC make_image=(PFNEGLCREATEIMAGEKHRPROC)resolve(egl,"eglCreateImageKHR");
    PFNEGLDESTROYSURFACEPROC destroy_pbuffer=API(egl,eglDestroySurface);
    PFNEGLDESTROYCONTEXTPROC destroy_context=API(egl,eglDestroyContext);
    PFNEGLTERMINATEPROC terminate=API(egl,eglTerminate);
    int (*allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **)=
        (int (*)(const AHardwareBuffer_Desc *, AHardwareBuffer **))
        resolve(nwin,"AHardwareBuffer_allocate");
    int (*send_ahb)(const AHardwareBuffer *, int)=
        (int (*)(const AHardwareBuffer *,int))
        resolve(nwin,"AHardwareBuffer_sendHandleToUnixSocket");
    void (*release_ahb)(AHardwareBuffer *)=
        (void (*)(AHardwareBuffer *))resolve(nwin,"AHardwareBuffer_release");
    EGLClientBuffer (*native_client)(const AHardwareBuffer *) =
        (EGLClientBuffer (*)(const AHardwareBuffer *))
        proc("eglGetNativeClientBufferANDROID");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target=
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)proc("glEGLImageTargetTexture2DOES");
    void (*gen_tex)(GLsizei,GLuint *)=API(gles,glGenTextures);
    void (*bind_tex)(GLenum,GLuint)=API(gles,glBindTexture);
    void (*tex_param)(GLenum,GLenum,GLint)=API(gles,glTexParameteri);
    void (*gen_fbo)(GLsizei,GLuint *)=API(gles,glGenFramebuffers);
    void (*bind_fbo)(GLenum,GLuint)=API(gles,glBindFramebuffer);
    void (*attach_fbo)(GLenum,GLenum,GLenum,GLuint,GLint)=API(gles,glFramebufferTexture2D);
    GLenum (*check_fbo)(GLenum)=API(gles,glCheckFramebufferStatus);
    void (*viewport)(GLint,GLint,GLsizei,GLsizei)=API(gles,glViewport);
    void (*scissor)(GLint,GLint,GLsizei,GLsizei)=API(gles,glScissor);
    void (*enable)(GLenum)=API(gles,glEnable);
    void (*clear_color)(GLfloat,GLfloat,GLfloat,GLfloat)=API(gles,glClearColor);
    void (*clear)(GLbitfield)=API(gles,glClear);
    void (*finish)(void)=API(gles,glFinish);
    GLenum (*get_error)(void)=API(gles,glGetError);
    void (*delete_tex)(GLsizei,const GLuint *)=API(gles,glDeleteTextures);
    void (*delete_fbo)(GLsizei,const GLuint *)=API(gles,glDeleteFramebuffers);
    if (!display_get || !initialize || !choose || !bind_api || !make_pbuffer ||
        !make_context || !current || !proc || !release_image || !make_image ||
        !destroy_pbuffer || !destroy_context || !terminate || !allocate ||
        !send_ahb || !release_ahb || !native_client || !image_target ||
        !gen_tex || !bind_tex || !tex_param || !gen_fbo || !bind_fbo ||
        !attach_fbo || !check_fbo || !viewport || !scissor || !enable ||
        !clear_color || !clear || !finish || !get_error || !delete_tex ||
        !delete_fbo) {
        puts("AHB_EGL_WINDOW=REQUIRED_SYMBOL_MISSING");
        goto libs;
    }

    EGLDisplay edpy=display_get(EGL_DEFAULT_DISPLAY);
    EGLint major=0, minor=0;
    if (edpy==EGL_NO_DISPLAY || !initialize(edpy,&major,&minor)) {
        puts("AHB_EGL_WINDOW=EGL_INITIALIZE_FAILED");
        goto libs;
    }
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    EGLImageKHR image=EGL_NO_IMAGE_KHR;
    AHardwareBuffer *ahb=NULL;
    GLuint texture=0, framebuffer=0;
    EGLint cfg_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
                        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLint pbuf_attrs[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLConfig cfg=NULL; EGLint num_cfg=0;
    if (!bind_api(EGL_OPENGL_ES_API) ||
        !choose(edpy,cfg_attrs,&cfg,1,&num_cfg) || num_cfg < 1) goto done;
    context=make_context(edpy,cfg,EGL_NO_CONTEXT,context_attrs);
    surface=make_pbuffer(edpy,cfg,pbuf_attrs);
    if (context==EGL_NO_CONTEXT || surface==EGL_NO_SURFACE ||
        !current(edpy,surface,surface,context)) goto done;
    AHardwareBuffer_Desc desc={0};
    desc.width=width; desc.height=height; desc.layers=1;
    desc.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if (allocate(&desc,&ahb) || !ahb) { puts("AHB_EGL_WINDOW=AHB_ALLOC_FAILED"); goto done; }
    EGLClientBuffer native=native_client(ahb);
    if (!native) goto done;
    image=make_image(edpy,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native,NULL);
    if (image==EGL_NO_IMAGE_KHR) goto done;
    gen_tex(1,&texture);
    bind_tex(GL_TEXTURE_2D,texture);
    tex_param(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    tex_param(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    image_target(GL_TEXTURE_2D,image);
    gen_fbo(1,&framebuffer);
    bind_fbo(GL_FRAMEBUFFER,framebuffer);
    attach_fbo(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if (check_fbo(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        puts("AHB_EGL_WINDOW=NOT_RENDERABLE_AS_FBO");
        goto done;
    }
    viewport(0,0,(int)width,(int)height);
    enable(GL_SCISSOR_TEST);
    const GLfloat rgba[4][4]={
        {0.98f,0.16f,0.12f,1.0f},{0.10f,0.86f,0.20f,1.0f},
        {0.12f,0.25f,0.98f,1.0f},{0.97f,0.88f,0.20f,1.0f}
    };
    for (int i=0;i<4;i++) {
        scissor((i%2)*(int)(width/2),(i/2)*(int)(height/2),
                (int)(width/2),(int)(height/2));
        clear_color(rgba[i][0],rgba[i][1],rgba[i][2],rgba[i][3]);
        clear(GL_COLOR_BUFFER_BIT);
    }
    /* Explicit first-stage correctness sync; optimization deferred. */
    finish();
    if (get_error()!=GL_NO_ERROR) {
        puts("AHB_EGL_WINDOW=GL_RENDER_FAILED");
        goto done;
    }
    int sp[2];
    if (socketpair(AF_UNIX,SOCK_STREAM,0,sp)!=0) goto done;
    int rc=send_ahb(ahb,sp[0]);
    close(sp[0]);
    if (rc!=0) { close(sp[1]); goto done; }
    d->buffer=alroot_gpu_buffer_v1_create_buffer(d->manager,sp[1],width,height);
    close(sp[1]);
    if (!d->buffer) goto done;
    wl_buffer_add_listener(d->buffer,&buffer_listener,d);
    puts("AHB_EGL_WINDOW=FRAME_SENT_TO_WAYLAND");
    puts("AHB_EGL_WINDOW_CPU_PIXEL_COPY=0");
    puts("AHB_EGL_WINDOW_HOST_VKIMAGE_PRESENT=CHECK_ANDROID_HOST_LOG");
    result=0;

done:
    if (framebuffer) delete_fbo(1,&framebuffer);
    if (texture) delete_tex(1,&texture);
    if (image!=EGL_NO_IMAGE_KHR) release_image(edpy,image);
    if (ahb) release_ahb(ahb);
    current(edpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if (context!=EGL_NO_CONTEXT) destroy_context(edpy,context);
    if (surface!=EGL_NO_SURFACE) destroy_pbuffer(edpy,surface);
    terminate(edpy);
libs:
    if (nwin) dlclose(nwin);
    if (gles) dlclose(gles);
    if (egl) dlclose(egl);
    return result;
}

int main(int argc,char **argv) {
    int seconds=argc>1?atoi(argv[1]):8;
    if (seconds<2) seconds=2;
    if (seconds>45) seconds=45;
    struct demo d={0};
    d.display=wl_display_connect(NULL);
    if (!d.display) {
        puts("AHB_EGL_WINDOW=NO_WAYLAND_CONNECTION");
        return 2;
    }
    struct wl_registry *registry=wl_display_get_registry(d.display);
    wl_registry_add_listener(registry,&registry_listener,&d);
    wl_display_roundtrip(d.display);
    if (!d.compositor || !d.wm || !d.manager) {
        puts("AHB_EGL_WINDOW=PROTOCOL_NOT_ADVERTISED");
        if (d.manager) alroot_gpu_buffer_v1_destroy(d.manager);
        if (d.wm) xdg_wm_base_destroy(d.wm);
        if (d.compositor) wl_compositor_destroy(d.compositor);
        wl_registry_destroy(registry);
        wl_display_disconnect(d.display);
        return 3;
    }
    xdg_wm_base_add_listener(d.wm,&wm_listener,&d);
    d.surface=wl_compositor_create_surface(d.compositor);
    d.xdg_surface=xdg_wm_base_get_xdg_surface(d.wm,d.surface);
    xdg_surface_add_listener(d.xdg_surface,&xdg_listener,&d);
    d.toplevel=xdg_surface_get_toplevel(d.xdg_surface);
    xdg_toplevel_add_listener(d.toplevel,&top_listener,&d);
    xdg_toplevel_set_title(d.toplevel,"Alroot universal GPU-copy test");
    xdg_toplevel_set_app_id(d.toplevel,"alroot.gpu.copy.test");
    wl_surface_commit(d.surface);
    for (int i=0;i<12 && !d.configured;i++)
        if (wl_display_roundtrip(d.display)<0) break;
    if (!d.configured || render_and_transfer(&d,640,360)!=0) {
        puts("AHB_EGL_WINDOW=INIT_FAILED");
        return 3;
    }
    wl_surface_attach(d.surface,d.buffer,0,0);
    wl_surface_damage_buffer(d.surface,0,0,640,360);
    wl_surface_commit(d.surface);
    wl_display_flush(d.display);
    puts("AHB_EGL_WINDOW=FRAME_COMMITTED");
    fflush(stdout);
    time_t until=time(NULL)+seconds;
    while (time(NULL)<until && !d.closed) {
        struct pollfd pf={ .fd=wl_display_get_fd(d.display), .events=POLLIN };
        int ready=poll(&pf,1,90);
        if (ready>0 && wl_display_dispatch(d.display)<0) break;
        wl_display_flush(d.display);
    }
    printf("AHB_EGL_WINDOW_BUFFER_RELEASE_EVENTS=%d\n",d.released);
    if (d.buffer) wl_buffer_destroy(d.buffer);
    xdg_toplevel_destroy(d.toplevel);
    xdg_surface_destroy(d.xdg_surface);
    wl_surface_destroy(d.surface);
    alroot_gpu_buffer_v1_destroy(d.manager);
    xdg_wm_base_destroy(d.wm);
    wl_compositor_destroy(d.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(d.display);
    return 0;
}
