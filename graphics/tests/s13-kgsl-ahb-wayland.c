/*
 * GPU-S1.3 experimental Freedreno KGSL -> shared AHB -> Wayland presenter.
 * No CPU pixel copying or global Mesa modification. Unlike regular KGSL
 * EGL Wayland, this test explicitly renders into external memory objects.
 * Requires the separately tested KGSL/AHB linear image import capability.
 */
#define _GNU_SOURCE
#include "alroot_ahb_wayland_presenter.h"
#include "alroot_kgsl_ahb_fd.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "viewporter-client-protocol.h"

#define LOAD(lib,sym) ((typeof(&sym))dlsym((lib),#sym))

struct demo {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wp_viewporter *viewporter;
    struct wp_viewport *viewport;
    struct xdg_wm_base *wm;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    int configured,closed;
};
static void global(void *data,struct wl_registry *reg,uint32_t id,
                   const char *iface,uint32_t ver) {
    struct demo *d=data;
    if(!strcmp(iface,wl_compositor_interface.name))
        d->compositor=wl_registry_bind(reg,id,&wl_compositor_interface,
                                       ver<4?ver:4);
    if(!strcmp(iface,wp_viewporter_interface.name))
        d->viewporter=wl_registry_bind(reg,id,&wp_viewporter_interface,1);
    if(!strcmp(iface,xdg_wm_base_interface.name))
        d->wm=wl_registry_bind(reg,id,&xdg_wm_base_interface,1);
}
static void gone(void *d,struct wl_registry *r,uint32_t id) {
    (void)d;(void)r;(void)id;
}
static const struct wl_registry_listener reg_listener={.global=global,.global_remove=gone};
static void ping(void *d,struct xdg_wm_base *wm,uint32_t serial) {
    (void)d;xdg_wm_base_pong(wm,serial);
}
static const struct xdg_wm_base_listener wm_listener={.ping=ping};
static void surface_configure(void *d,struct xdg_surface *s,uint32_t serial) {
    ((struct demo *)d)->configured=1;xdg_surface_ack_configure(s,serial);
}
static const struct xdg_surface_listener surf_listener={.configure=surface_configure};
static void tl_configure(void *d,struct xdg_toplevel *t,int32_t w,int32_t h,
                         struct wl_array *states) {
    (void)d;(void)t;(void)w;(void)h;(void)states;
}
static void tl_close(void *d,struct xdg_toplevel *t) {
    (void)t;((struct demo *)d)->closed=1;
}
static const struct xdg_toplevel_listener tl_listener={
    .configure=tl_configure,.close=tl_close
};

struct egl_api {
    void *egl,*gles,*native;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    PFNEGLGETDISPLAYPROC get_display;
    PFNEGLINITIALIZEPROC initialize;
    PFNEGLCHOOSECONFIGPROC choose;
    PFNEGLBINDAPIPROC bind_api;
    PFNEGLCREATECONTEXTPROC create_context;
    PFNEGLCREATEPBUFFERSURFACEPROC create_pbuffer;
    PFNEGLMAKECURRENTPROC make_current;
    PFNEGLDESTROYCONTEXTPROC destroy_context;
    PFNEGLDESTROYSURFACEPROC destroy_pbuffer;
    PFNEGLTERMINATEPROC terminate;
    PFNEGLGETPROCADDRESSPROC get_proc;
    PFNGLCREATEMEMORYOBJECTSEXTPROC create_memory;
    PFNGLIMPORTMEMORYFDEXTPROC import_memory_fd;
    PFNGLTEXSTORAGEMEM2DEXTPROC texture_memory;
    PFNGLDELETEMEMORYOBJECTSEXTPROC delete_memory;
    int (*allocate)(const AHardwareBuffer_Desc *,AHardwareBuffer **);
    void (*release)(AHardwareBuffer *);
    void (*gen_textures)(GLsizei,GLuint *);
    void (*bind_texture)(GLenum,GLuint);
    void (*tex_param)(GLenum,GLenum,GLint);
    void (*delete_textures)(GLsizei,const GLuint *);
    void (*gen_fbos)(GLsizei,GLuint *);
    void (*bind_fbo)(GLenum,GLuint);
    void (*attach)(GLenum,GLenum,GLenum,GLuint,GLint);
    GLenum (*check)(GLenum);
    void (*delete_fbos)(GLsizei,const GLuint *);
    void (*clear_color)(GLfloat,GLfloat,GLfloat,GLfloat);
    void (*clear)(GLbitfield);
    void (*viewport)(GLint,GLint,GLsizei,GLsizei);
    void (*finish)(void);
    GLenum (*gl_error)(void);
    const GLubyte *(*gl_string)(GLenum);
};

static int api_init(struct egl_api *a) {
    a->egl=dlopen("/data/data/com.termux/files/usr/lib/libEGL.so",RTLD_NOW|RTLD_LOCAL);
    a->gles=dlopen("/data/data/com.termux/files/usr/lib/libGLESv2.so",RTLD_NOW|RTLD_LOCAL);
    a->native=dlopen("/system/lib64/libnativewindow.so",RTLD_NOW|RTLD_LOCAL);
    if(!a->egl||!a->gles||!a->native)return -1;
    a->get_display=LOAD(a->egl,eglGetDisplay);
    a->initialize=LOAD(a->egl,eglInitialize);
    a->choose=LOAD(a->egl,eglChooseConfig);
    a->bind_api=LOAD(a->egl,eglBindAPI);
    a->create_context=LOAD(a->egl,eglCreateContext);
    a->create_pbuffer=LOAD(a->egl,eglCreatePbufferSurface);
    a->make_current=LOAD(a->egl,eglMakeCurrent);
    a->destroy_context=LOAD(a->egl,eglDestroyContext);
    a->destroy_pbuffer=LOAD(a->egl,eglDestroySurface);
    a->terminate=LOAD(a->egl,eglTerminate);
    a->get_proc=LOAD(a->egl,eglGetProcAddress);
    a->allocate=(int (*)(const AHardwareBuffer_Desc *,AHardwareBuffer **))
        dlsym(a->native,"AHardwareBuffer_allocate");
    a->release=(void (*)(AHardwareBuffer *))
        dlsym(a->native,"AHardwareBuffer_release");
    a->gen_textures=LOAD(a->gles,glGenTextures);
    a->bind_texture=LOAD(a->gles,glBindTexture);
    a->tex_param=LOAD(a->gles,glTexParameteri);
    a->delete_textures=LOAD(a->gles,glDeleteTextures);
    a->gen_fbos=LOAD(a->gles,glGenFramebuffers);
    a->bind_fbo=LOAD(a->gles,glBindFramebuffer);
    a->attach=LOAD(a->gles,glFramebufferTexture2D);
    a->check=LOAD(a->gles,glCheckFramebufferStatus);
    a->delete_fbos=LOAD(a->gles,glDeleteFramebuffers);
    a->clear_color=LOAD(a->gles,glClearColor);
    a->clear=LOAD(a->gles,glClear);
    a->viewport=LOAD(a->gles,glViewport);
    a->finish=LOAD(a->gles,glFinish);
    a->gl_error=LOAD(a->gles,glGetError);
    a->gl_string=LOAD(a->gles,glGetString);
    if(!a->get_display||!a->initialize||!a->choose||!a->bind_api||
       !a->create_context||!a->create_pbuffer||!a->make_current||
       !a->destroy_context||!a->destroy_pbuffer||!a->terminate||
       !a->get_proc||!a->allocate||!a->release||!a->gen_textures||
       !a->bind_texture||!a->tex_param||!a->delete_textures||
       !a->gen_fbos||!a->bind_fbo||!a->attach||!a->check||
       !a->delete_fbos||!a->clear_color||!a->clear||!a->viewport||
       !a->finish||!a->gl_error||!a->gl_string) return -1;
    a->create_memory=(PFNGLCREATEMEMORYOBJECTSEXTPROC)
        a->get_proc("glCreateMemoryObjectsEXT");
    a->import_memory_fd=(PFNGLIMPORTMEMORYFDEXTPROC)
        a->get_proc("glImportMemoryFdEXT");
    a->texture_memory=(PFNGLTEXSTORAGEMEM2DEXTPROC)
        a->get_proc("glTexStorageMem2DEXT");
    a->delete_memory=(PFNGLDELETEMEMORYOBJECTSEXTPROC)
        a->get_proc("glDeleteMemoryObjectsEXT");
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform=
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)a->get_proc("eglGetPlatformDisplayEXT");
    if(!a->create_memory||!a->import_memory_fd||!a->texture_memory||
       !a->delete_memory||!platform) return -1;
    a->display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
    EGLint maj=0,min=0;
    if(a->display==EGL_NO_DISPLAY || !a->initialize(a->display,&maj,&min))
        return -1;
    EGLint cfgattr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
                      EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig config=NULL;
    EGLint n=0;
    if(!a->bind_api(EGL_OPENGL_ES_API)||
       !a->choose(a->display,cfgattr,&config,1,&n)||n<1) return -1;
    EGLint ctxattr[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLint pbufferattr[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    a->context=a->create_context(a->display,config,EGL_NO_CONTEXT,ctxattr);
    a->surface=a->create_pbuffer(a->display,config,pbufferattr);
    if(a->context==EGL_NO_CONTEXT||a->surface==EGL_NO_SURFACE||
       !a->make_current(a->display,a->surface,a->surface,a->context))
        return -1;
    const char *vendor=(const char *)a->gl_string(GL_VENDOR);
    const char *renderer=(const char *)a->gl_string(GL_RENDERER);
    if(!vendor||strcmp(vendor,"freedreno")||!renderer||
       !strstr(renderer,"Adreno")) {
        printf("S13_KGSL_EXPECTED_FREEDRENO=FAIL vendor=%s renderer=%s\n",
               vendor?vendor:"unknown",renderer?renderer:"unknown");
        return -1;
    }
    printf("S13_KGSL_GLES_RENDERER=%s\n",renderer);
    return 0;
}

struct image {
    AHardwareBuffer *ahb;
    GLuint memory,texture,fbo;
};
static int image_init(struct egl_api *a,struct image *img,
                      uint32_t width,uint32_t height,
                      uint32_t allocation_height,uint32_t format) {
    AHardwareBuffer_Desc desc={0};
    desc.width=width;desc.height=allocation_height;desc.layers=1;
    desc.format=format;
    desc.usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if(a->allocate(&desc,&img->ahb)!=0||!img->ahb)return -1;
    uint64_t allocation_size=0;
    uint32_t stride=0;
    int fd=alroot_kgsl_ahb_dup_image_fd(img->ahb,&allocation_size,&stride);
    if(fd<0) {
        printf("S13_KGSL_AHB_FD_SELECTION=UNSUPPORTED_%d\n",fd);
        return -1;
    }
    a->create_memory(1,&img->memory);
    if(!img->memory) {close(fd);return -1;}
    a->import_memory_fd(img->memory,allocation_size,GL_HANDLE_TYPE_OPAQUE_FD_EXT,fd);
    if(a->gl_error()!=GL_NO_ERROR) {close(fd);return -1;}
    a->gen_textures(1,&img->texture);
    a->bind_texture(GL_TEXTURE_2D,img->texture);
    a->tex_param(GL_TEXTURE_2D,GL_TEXTURE_TILING_EXT,GL_LINEAR_TILING_EXT);
    if(a->gl_error()!=GL_NO_ERROR)return -1;
    /* KGSL's linear imported texture needs its own row pitch, supplied
     * by the AHB stride. Pad the backing allocation height separately to
     * cover KGSL's tile/row-tail storage requirement. Crop with Wayland
     * viewporter: do not expose the padding as desktop resolution. */
    a->texture_memory(GL_TEXTURE_2D,1,GL_RGBA8,stride,height,img->memory,0);
    if(a->gl_error()!=GL_NO_ERROR)return -1;
    a->gen_fbos(1,&img->fbo);
    a->bind_fbo(GL_FRAMEBUFFER,img->fbo);
    a->attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,img->texture,0);
    if(a->check(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE ||
       a->gl_error()!=GL_NO_ERROR)return -1;
    printf("S13_KGSL_AHB_IMAGE=READY logical=%ux%u backing=%ux%u stride=%u size=%llu\n",
           width,height,width,allocation_height,stride,
           (unsigned long long)allocation_size);
    return 0;
}
static void image_free(struct egl_api *a,struct image *img) {
    if(img->fbo)a->delete_fbos(1,&img->fbo);
    if(img->texture)a->delete_textures(1,&img->texture);
    if(img->memory)a->delete_memory(1,&img->memory);
    if(img->ahb)a->release(img->ahb);
}
static void api_cleanup(struct egl_api *a) {
    if(a->display&&a->make_current)
        a->make_current(a->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(a->context&&a->context!=EGL_NO_CONTEXT&&a->destroy_context)
        a->destroy_context(a->display,a->context);
    if(a->surface&&a->surface!=EGL_NO_SURFACE&&a->destroy_pbuffer)
        a->destroy_pbuffer(a->display,a->surface);
    if(a->display&&a->display!=EGL_NO_DISPLAY&&a->terminate)
        a->terminate(a->display);
    if(a->native)dlclose(a->native);
    if(a->gles)dlclose(a->gles);
    if(a->egl)dlclose(a->egl);
}

int main(int argc,char **argv) {
    const int count=argc>1?atoi(argv[1]):90;
    if(count<1||count>360) return 2;
    const uint32_t width=480,height=270;
    const uint32_t allocation_height=(height+63u)&~63u;
    uint32_t format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    printf("S13_KGSL_AHB_FORMAT=RGBA8\n");
    struct demo d={0};
    d.display=wl_display_connect(NULL);
    if(!d.display) {puts("S13_GLES_AHB=NO_WAYLAND");return 3;}
    struct wl_registry *reg=wl_display_get_registry(d.display);
    wl_registry_add_listener(reg,&reg_listener,&d);
    if(wl_display_roundtrip(d.display)<0||!d.compositor||!d.wm||
       !d.viewporter) {
        puts("S13_KGSL_VIEWPORT=UNAVAILABLE");
        return 3;
    }
    xdg_wm_base_add_listener(d.wm,&wm_listener,&d);
    d.surface=wl_compositor_create_surface(d.compositor);
    d.viewport=wp_viewporter_get_viewport(d.viewporter,d.surface);
    if(!d.viewport)return 3;
    wp_viewport_set_source(d.viewport,wl_fixed_from_int(0),wl_fixed_from_int(0),
                           wl_fixed_from_int((int)width),
                           wl_fixed_from_int((int)height));
    wp_viewport_set_destination(d.viewport,(int)width,(int)height);
    printf("S13_KGSL_WAYLAND_VIEWPORT=%ux%u backing-height=%u\n",
           width,height,allocation_height);
    d.xdg_surface=xdg_wm_base_get_xdg_surface(d.wm,d.surface);
    xdg_surface_add_listener(d.xdg_surface,&surf_listener,&d);
    d.toplevel=xdg_surface_get_toplevel(d.xdg_surface);
    xdg_toplevel_add_listener(d.toplevel,&tl_listener,&d);
    xdg_toplevel_set_title(d.toplevel,"Alroot S1.3 KGSL AHB presenter");
    xdg_toplevel_set_app_id(d.toplevel,"alroot.s13.kgsl.ahb");
    wl_surface_commit(d.surface);
    if(wl_display_roundtrip(d.display)<0||!d.configured)return 3;
    struct alroot_ahb_wl_presenter *presenter=
        alroot_ahb_wl_presenter_create(d.display,d.surface);
    if(!presenter) {
        puts("S13_GLES_AHB=ALROOT_GPU_BUFFER_PROTOCOL_MISSING");
        return 3;
    }
    struct egl_api a={0};
    if(api_init(&a)!=0) {
        puts("S13_GLES_AHB=SYSTEM_EGL_INITIALIZE_FAILED");
        return 3;
    }
    struct image images[3]={0};
    for(int i=0;i<3;i++)
        if(image_init(&a,&images[i],width,height,allocation_height,format)!=0) {
            printf("S13_GLES_AHB=IMAGE_%d_INIT_FAILED\n",i);
            return 3;
        }
    int complete=0,timeout_count=0;
    for(int i=0;i<count&&!d.closed;i++) {
        struct image *img=&images[i%3];
        int wait=0;
        while(alroot_ahb_wl_presenter_is_available(presenter,img->ahb)==0) {
            if(++wait>100) {timeout_count++;break;}
            if(alroot_ahb_wl_presenter_dispatch(presenter,30)<0) break;
        }
        if(wait>100)break;
        a.bind_fbo(GL_FRAMEBUFFER,img->fbo);
        a.viewport(0,0,width,height);
        float f=(float)(i%90)/89.f;
        a.clear_color(f,0.18f,1.f-f,1.f);
        a.clear(GL_COLOR_BUFFER_BIT);
        /* Stage-1 correctness: no explicit cross-process acquire fence
         * in protocol v1. This CPU wait is deliberately retained. */
        a.finish();
        if(a.gl_error()!=GL_NO_ERROR)break;
        int rc=alroot_ahb_wl_presenter_submit(presenter,img->ahb,
                                              width,allocation_height);
        if(rc!=0) {
            printf("S13_KGSL_AHB_SUBMIT_ERROR=%d\n",rc);
            break;
        }
        complete++;
    }
    wl_surface_attach(d.surface,NULL,0,0);
    wl_surface_commit(d.surface);
    wl_display_flush(d.display);
    for(int i=0;i<35;i++)
        alroot_ahb_wl_presenter_dispatch(presenter,30);
    const struct alroot_ahb_wl_stats stats=
        alroot_ahb_wl_presenter_stats(presenter);
    printf("S13_KGSL_AHB_FRAMES=%d requested=%d\n",complete,count);
    printf("S13_KGSL_AHB_STATS registered=%llu submitted=%llu released=%llu reused=%llu busy_rejects=%llu live_slots=%u\n",
           (unsigned long long)stats.registered,
           (unsigned long long)stats.submitted,
           (unsigned long long)stats.released,
           (unsigned long long)stats.reused,
           (unsigned long long)stats.busy_rejects,stats.live_slots);
    printf("S13_KGSL_AHB_GPU_SUBMISSION=%s timeout_count=%d\n",
           complete==count?"PASS":"FAIL",timeout_count);
    alroot_ahb_wl_presenter_destroy(presenter);
    for(int i=0;i<3;i++)image_free(&a,&images[i]);
    api_cleanup(&a);
    xdg_toplevel_destroy(d.toplevel);
    xdg_surface_destroy(d.xdg_surface);
    wp_viewport_destroy(d.viewport);
    wl_surface_destroy(d.surface);
    wp_viewporter_destroy(d.viewporter);
    xdg_wm_base_destroy(d.wm);
    wl_compositor_destroy(d.compositor);
    wl_registry_destroy(reg);
    wl_display_disconnect(d.display);
    return complete==count && stats.submitted==(uint64_t)count &&
           stats.released==(uint64_t)count && stats.busy_rejects==0?0:5;
}
