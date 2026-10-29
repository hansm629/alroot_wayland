#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "alroot_ahb_wayland_presenter.h"
#include "alroot_kgsl_ahb_fd.h"
#include "viewporter-client-protocol.h"

/* GPU-S1.3-B: private, opt-in EGL shim EXPERIMENT. This is NOT full
 * EGL buffer-age/present semantics; never install or preload globally.
 * Only an isolated KGSL GLES3 Wayland validation client may enable it. */
#define KGSL_SHIM_MAX_WINDOWS 4
#define KGSL_SHIM_BUFFERS 3
struct kgsl_egl_slot {
    AHardwareBuffer *ahb;
    GLuint memory, texture, fbo;
};
struct kgsl_egl_surface {
    struct wl_egl_window *win;
    struct wl_surface *surface;
    struct wl_display *display;
    struct wl_event_queue *queue;
    struct wl_registry *registry;
    struct wp_viewporter *viewporter;
    struct wp_viewport *viewport;
    struct alroot_ahb_wl_presenter *presenter;
    EGLSurface egl_surface;
    uint32_t width,height,padded_height;
    struct kgsl_egl_slot slots[KGSL_SHIM_BUFFERS];
    unsigned serial;
    int ready;
};
static struct kgsl_egl_surface g_windows[KGSL_SHIM_MAX_WINDOWS];
static void *g_native;
static int (*g_alloc)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
static void (*g_release)(AHardwareBuffer *);
static PFNGLCREATEMEMORYOBJECTSEXTPROC g_create_mem;
static PFNGLIMPORTMEMORYFDEXTPROC g_import_fd;
static PFNGLTEXSTORAGEMEM2DEXTPROC g_storage;
static PFNGLDELETEMEMORYOBJECTSEXTPROC g_delete_mem;

static int enabled(void) {
    const char *v=getenv("ALROOT_S13_KGSL_EGL_AHB");
    return v && strcmp(v,"1")==0;
}
static void *resolve_next(const char *name) {
    return dlsym(RTLD_NEXT,name);
}
static struct kgsl_egl_surface *get_by_window(struct wl_egl_window *win) {
    if(!win)return NULL;
    for(int i=0;i<KGSL_SHIM_MAX_WINDOWS;i++)
        if(g_windows[i].win==win) return &g_windows[i];
    return NULL;
}
static struct kgsl_egl_surface *get_by_egl(EGLSurface egl) {
    if(egl==EGL_NO_SURFACE)return NULL;
    for(int i=0;i<KGSL_SHIM_MAX_WINDOWS;i++)
        if(g_windows[i].win && g_windows[i].egl_surface==egl)
            return &g_windows[i];
    return NULL;
}
struct wl_egl_window *wl_egl_window_create(struct wl_surface *surface,int width,int height) {
    struct wl_egl_window *(*orig)(struct wl_surface *,int,int)=
        resolve_next("wl_egl_window_create");
    if(!orig)return NULL;
    struct wl_egl_window *win=orig(surface,width,height);
    if(!win || !enabled())return win;
    for(int i=0;i<KGSL_SHIM_MAX_WINDOWS;i++)
        if(!g_windows[i].win) {
            struct kgsl_egl_surface *s=&g_windows[i];
            memset(s,0,sizeof(*s));
            s->win=win;
            s->surface=surface;
            s->display=wl_proxy_get_display((struct wl_proxy *)surface);
            s->width=(uint32_t)width;
            s->height=(uint32_t)height;
            break;
        }
    return win;
}
void wl_egl_window_resize(struct wl_egl_window *win,int width,int height,int dx,int dy) {
    void (*orig)(struct wl_egl_window *,int,int,int,int)=
        resolve_next("wl_egl_window_resize");
    if(orig)orig(win,width,height,dx,dy);
    struct kgsl_egl_surface *s=get_by_window(win);
    if(s && width>0 && height>0) {
        s->width=(uint32_t)width;
        s->height=(uint32_t)height;
        if(s->ready) s->ready=2; /* later swap refuses stale backing */
    }
}
void wl_egl_window_destroy(struct wl_egl_window *win) {
    void (*orig)(struct wl_egl_window *)=resolve_next("wl_egl_window_destroy");
    struct kgsl_egl_surface *s=get_by_window(win);
    if(s) {
        /* EGL surface should already be destroyed; unexpected live state
         * is not silently reused with an unrelated new wl_surface. */
        if(s->presenter || s->egl_surface)
            fprintf(stderr,"S13_EGL_SHIM=WARNING_WINDOW_DESTROY_BEFORE_EGL\n");
        memset(s,0,sizeof(*s));
    }
    if(orig)orig(win);
}
EGLSurface eglCreateWindowSurface(EGLDisplay dpy,EGLConfig cfg,
                                  EGLNativeWindowType win,const EGLint *attrs) {
    EGLSurface (*orig)(EGLDisplay,EGLConfig,EGLNativeWindowType,const EGLint *)=
        resolve_next("eglCreateWindowSurface");
    if(!orig)return EGL_NO_SURFACE;
    struct kgsl_egl_surface *s=enabled()?
        get_by_window((struct wl_egl_window *)win):NULL;
    if(enabled() && !s) {
        fprintf(stderr,"S13_EGL_SHIM=UNSUPPORTED_WINDOW_REFUSED_NO_DMA_BUF_FALLBACK\n");
        return EGL_NO_SURFACE;
    }
    EGLSurface result=orig(dpy,cfg,win,attrs);
    if(result!=EGL_NO_SURFACE && s)s->egl_surface=result;
    return result;
}
static void viewport_global(void *data, struct wl_registry *reg,uint32_t name,
                            const char *interface,uint32_t ver) {
    struct kgsl_egl_surface *s=data;
    if(!strcmp(interface,wp_viewporter_interface.name) && !s->viewporter && ver)
        s->viewporter=wl_registry_bind(reg,name,&wp_viewporter_interface,1);
}
static void viewport_gone(void *data,struct wl_registry *r,uint32_t name) {
    (void)data;(void)r;(void)name;
}
static const struct wl_registry_listener vp_listener={
    .global=viewport_global,.global_remove=viewport_gone
};
static int init_gl_api(void) {
    if(!g_native)g_native=dlopen("/system/lib64/libnativewindow.so",RTLD_NOW|RTLD_LOCAL);
    if(!g_native)return -1;
    g_alloc=dlsym(g_native,"AHardwareBuffer_allocate");
    g_release=dlsym(g_native,"AHardwareBuffer_release");
    g_create_mem=(PFNGLCREATEMEMORYOBJECTSEXTPROC)
        eglGetProcAddress("glCreateMemoryObjectsEXT");
    g_import_fd=(PFNGLIMPORTMEMORYFDEXTPROC)
        eglGetProcAddress("glImportMemoryFdEXT");
    g_storage=(PFNGLTEXSTORAGEMEM2DEXTPROC)
        eglGetProcAddress("glTexStorageMem2DEXT");
    g_delete_mem=(PFNGLDELETEMEMORYOBJECTSEXTPROC)
        eglGetProcAddress("glDeleteMemoryObjectsEXT");
    if(!g_alloc||!g_release||!g_create_mem||!g_import_fd||
       !g_storage||!g_delete_mem)return -1;
    const char *vendor=(const char *)glGetString(GL_VENDOR);
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    if(!vendor || strcmp(vendor,"freedreno") || !renderer ||
       !strstr(renderer,"Adreno"))return -1;
    fprintf(stderr,"S13_EGL_SHIM=KGSL_DRIVER_VERIFIED renderer=%s\n",renderer);
    return 0;
}
static int init_slot(struct kgsl_egl_surface *s,struct kgsl_egl_slot *slot) {
    AHardwareBuffer_Desc desc={.width=s->width,.height=s->padded_height,
        .layers=1,.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
    if(g_alloc(&desc,&slot->ahb)||!slot->ahb)return -1;
    uint64_t size=0;
    uint32_t stride=0;
    int fd=alroot_kgsl_ahb_dup_image_fd(slot->ahb,&size,&stride);
    if(fd<0)return -1;
    g_create_mem(1,&slot->memory);
    if(!slot->memory){close(fd);return -1;}
    g_import_fd(slot->memory,size,GL_HANDLE_TYPE_OPAQUE_FD_EXT,fd);
    if(glGetError()!=GL_NO_ERROR){close(fd);return -1;}
    glGenTextures(1,&slot->texture);
    glBindTexture(GL_TEXTURE_2D,slot->texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_TILING_EXT,GL_LINEAR_TILING_EXT);
    if(glGetError()!=GL_NO_ERROR)return -1;
    g_storage(GL_TEXTURE_2D,1,GL_RGBA8,stride,s->height,slot->memory,0);
    if(glGetError()!=GL_NO_ERROR)return -1;
    glGenFramebuffers(1,&slot->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,slot->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,slot->texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||
       glGetError()!=GL_NO_ERROR)return -1;
    return 0;
}
static int init_surface(struct kgsl_egl_surface *s) {
    if(init_gl_api()<0 || s->width!=480 || s->height!=270 || !s->display)
        return -1;
    s->padded_height=(s->height+63u)&~63u;
    s->queue=wl_display_create_queue_with_name(s->display,"alroot kgsl EGL viewport");
    if(!s->queue)return -1;
    s->registry=wl_display_get_registry(s->display);
    if(!s->registry)return -1;
    wl_proxy_set_queue((struct wl_proxy *)s->registry,s->queue);
    if(wl_registry_add_listener(s->registry,&vp_listener,s)<0||
       wl_display_roundtrip_queue(s->display,s->queue)<0||!s->viewporter)
        return -1;
    s->viewport=wp_viewporter_get_viewport(s->viewporter,s->surface);
    if(!s->viewport)return -1;
    wp_viewport_set_source(s->viewport,wl_fixed_from_int(0),wl_fixed_from_int(0),
        wl_fixed_from_int((int)s->width),wl_fixed_from_int((int)s->height));
    wp_viewport_set_destination(s->viewport,(int)s->width,(int)s->height);
    s->presenter=alroot_ahb_wl_presenter_create(s->display,s->surface);
    if(!s->presenter)return -1;
    GLint prev_tex=0,prev_fb=0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&prev_tex);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&prev_fb);
    for(int i=0;i<KGSL_SHIM_BUFFERS;i++) {
        if(init_slot(s,&s->slots[i])<0)return -1;
    }
    glBindTexture(GL_TEXTURE_2D,(GLuint)prev_tex);
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)prev_fb);
    s->ready=1;
    fprintf(stderr,"S13_EGL_SHIM=AHB_WAYLAND_READY logical=%ux%u backing=%ux%u\n",
            s->width,s->height,s->width,s->padded_height);
    return 0;
}
static int copy_and_present(struct kgsl_egl_surface *s) {
    if(s->ready==2)return -1;
    if(!s->ready && init_surface(s)<0) {
        s->ready=2;
        return -1;
    }
    struct kgsl_egl_slot *img=&s->slots[s->serial%KGSL_SHIM_BUFFERS];
    int available=alroot_ahb_wl_presenter_is_available(s->presenter,img->ahb);
    for(int wait=0;wait<100 && available==0;wait++) {
        if(alroot_ahb_wl_presenter_dispatch(s->presenter,20)<0)return -1;
        available=alroot_ahb_wl_presenter_is_available(s->presenter,img->ahb);
    }
    if(available!=1)return -1;
    GLint old_read=0,old_draw=0;
    GLboolean old_scissor=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
    if(old_scissor)glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,img->fbo);
    glBlitFramebuffer(0,0,(GLint)s->width,(GLint)s->height,
                      0,0,(GLint)s->width,(GLint)s->height,
                      GL_COLOR_BUFFER_BIT,GL_NEAREST);
    GLenum err=glGetError();
    glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)old_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)old_draw);
    if(old_scissor)glEnable(GL_SCISSOR_TEST);
    if(err!=GL_NO_ERROR){fprintf(stderr,"S13_EGL_SHIM=GPU_BLIT_ERROR_0x%x\n",err);return -1;}
    glFinish(); /* Stage-1 correctness; explicit GPU acquire fences come later. */
    if(glGetError()!=GL_NO_ERROR)return -1;
    if(alroot_ahb_wl_presenter_submit(s->presenter,img->ahb,
                                      s->width,s->padded_height)!=0)return -1;
    s->serial++;
    return 0;
}
EGLBoolean eglSwapBuffers(EGLDisplay dpy,EGLSurface egl_surface) {
    EGLBoolean (*orig)(EGLDisplay,EGLSurface)=resolve_next("eglSwapBuffers");
    if(!orig)return EGL_FALSE;
    if(!enabled())return orig(dpy,egl_surface);
    struct kgsl_egl_surface *s=get_by_egl(egl_surface);
    if(!s) {
        fprintf(stderr,"S13_EGL_SHIM=UNTRACKED_SURFACE_REFUSED_NO_DMA_BUF_FALLBACK\n");
        return EGL_FALSE;
    }
    if(copy_and_present(s)<0) {
        fprintf(stderr,"S13_EGL_SHIM=FAILED_CLOSED_NO_DMA_BUF_FALLBACK\n");
        return EGL_FALSE;
    }
    return EGL_TRUE;
}
EGLBoolean eglDestroySurface(EGLDisplay dpy,EGLSurface egl_surface) {
    EGLBoolean (*orig)(EGLDisplay,EGLSurface)=resolve_next("eglDestroySurface");
    struct kgsl_egl_surface *s=get_by_egl(egl_surface);
    if(s) {
        if(s->presenter) {
            wl_surface_attach(s->surface,NULL,0,0);
            wl_surface_commit(s->surface);
            wl_display_flush(s->display);
            for(int i=0;i<50;i++)
                alroot_ahb_wl_presenter_dispatch(s->presenter,20);
            struct alroot_ahb_wl_stats stats=alroot_ahb_wl_presenter_stats(s->presenter);
            fprintf(stderr,"S13_EGL_SHIM_STATS submitted=%llu released=%llu\n",
                    (unsigned long long)stats.submitted,
                    (unsigned long long)stats.released);
            alroot_ahb_wl_presenter_destroy(s->presenter);
            s->presenter=NULL;
        }
        if(s->viewport)wp_viewport_destroy(s->viewport);
        if(s->viewporter)wp_viewporter_destroy(s->viewporter);
        if(s->registry)wl_registry_destroy(s->registry);
        if(s->queue)wl_event_queue_destroy(s->queue);
        if(eglGetCurrentContext()!=EGL_NO_CONTEXT) {
            for(int i=0;i<KGSL_SHIM_BUFFERS;i++) {
                if(s->slots[i].fbo)glDeleteFramebuffers(1,&s->slots[i].fbo);
                if(s->slots[i].texture)glDeleteTextures(1,&s->slots[i].texture);
                if(s->slots[i].memory && g_delete_mem)
                    g_delete_mem(1,&s->slots[i].memory);
            }
        }
        for(int i=0;i<KGSL_SHIM_BUFFERS;i++)
            if(s->slots[i].ahb && g_release)g_release(s->slots[i].ahb);
        s->egl_surface=EGL_NO_SURFACE;
        s->ready=0;
    }
    return orig?orig(dpy,egl_surface):EGL_FALSE;
}
