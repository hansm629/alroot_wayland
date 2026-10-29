/*
 * GPU-S1.2: real EGL/OpenGL ES Wayland presentation probe.
 *
 * Uses the normal Termux/Mesa EGL Wayland frontend, so Zink can use its
 * Vulkan/Kopper WSI and Freedreno KGSL can reveal its own real buffer
 * export behavior. This is not an offscreen Pbuffer test, GLX, Xwayland,
 * CPU-upload shim, or simulated GPU pass.
 *
 * The caller selects the EGL/GL driver and Vulkan ICD in this process
 * only. No library installation, app replacement, DRM fd fabrication,
 * vendor-specific assumptions, or Termux-X11 DRI3 changes.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"

#ifndef EGL_PLATFORM_WAYLAND_EXT
#define EGL_PLATFORM_WAYLAND_EXT 0x31D8
#endif

struct client {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    int configured, closed;
    int width, height;
};

static int private_egl_library_mapped(void) {
    FILE *f=fopen("/proc/self/maps","r");
    if(!f)return 0;
    int found=0;
    const char *needle=getenv("ALROOT_PRIVATE_EGL_MAP_NEEDLE");
    if(!needle||!*needle)
        needle="/mesa-egl/private-runtime/lib/libEGL_mesa.so";
    char line[1024];
    while(fgets(line,sizeof(line),f))
        if(strstr(line,needle)) {
            found=1;
            break;
        }
    fclose(f);
    return found;
}

static void on_global(void *data,struct wl_registry *reg,uint32_t name,
                      const char *iface,uint32_t ver) {
    struct client *c=data;
    if (!strcmp(iface,wl_compositor_interface.name))
        c->compositor=wl_registry_bind(reg,name,&wl_compositor_interface,
                                       ver<4?ver:4);
    else if(!strcmp(iface,xdg_wm_base_interface.name))
        c->wm=wl_registry_bind(reg,name,&xdg_wm_base_interface,1);
}
static void on_removed(void *data,struct wl_registry *reg,uint32_t name) {
    (void)data;(void)reg;(void)name;
}
static const struct wl_registry_listener reg_listener={
    .global=on_global,.global_remove=on_removed
};
static void on_ping(void *data,struct xdg_wm_base *wm,uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm,serial);
}
static const struct xdg_wm_base_listener wm_listener={.ping=on_ping};
static void on_surface_configure(void *data,struct xdg_surface *surface,uint32_t serial) {
    struct client *c=data;
    xdg_surface_ack_configure(surface,serial);
    c->configured=1;
}
static const struct xdg_surface_listener surface_listener={
    .configure=on_surface_configure
};
static void on_toplevel_configure(void *data,struct xdg_toplevel *tl,
                                  int32_t width,int32_t height,
                                  struct wl_array *states) {
    (void)tl;(void)states;
    struct client *c=data;
    if (width>0 && height>0) {
        c->width=width;
        c->height=height;
    }
}
static void on_close(void *data,struct xdg_toplevel *tl) {
    (void)tl;
    ((struct client *)data)->closed=1;
}
static const struct xdg_toplevel_listener tl_listener={
    .configure=on_toplevel_configure,.close=on_close
};

int main(int argc,char **argv) {
    int frames=argc>1?atoi(argv[1]):90;
    if(frames<1||frames>600) {
        fprintf(stderr,"usage: %s [1..600 frames]\n",argv[0]);
        return 2;
    }
    struct client c={.width=480,.height=270};
    c.display=wl_display_connect(NULL);
    if(!c.display) {
        puts("S12_WAYLAND_EGL=NO_WAYLAND_CONNECTION");
        return 3;
    }
    struct wl_registry *reg=wl_display_get_registry(c.display);
    wl_registry_add_listener(reg,&reg_listener,&c);
    if(wl_display_roundtrip(c.display)<0||!c.compositor||!c.wm) {
        puts("S12_WAYLAND_EGL=NO_COMPOSITOR_OR_XDG");
        return 3;
    }
    xdg_wm_base_add_listener(c.wm,&wm_listener,&c);
    c.surface=wl_compositor_create_surface(c.compositor);
    c.xdg_surface=xdg_wm_base_get_xdg_surface(c.wm,c.surface);
    xdg_surface_add_listener(c.xdg_surface,&surface_listener,&c);
    c.toplevel=xdg_surface_get_toplevel(c.xdg_surface);
    xdg_toplevel_add_listener(c.toplevel,&tl_listener,&c);
    xdg_toplevel_set_title(c.toplevel,"Alroot S1.2 Wayland EGL/GLES swap test");
    xdg_toplevel_set_app_id(c.toplevel,"alroot.s12.egl");
    wl_surface_commit(c.surface);
    if(wl_display_roundtrip(c.display)<0||!c.configured) {
        puts("S12_WAYLAND_EGL=NO_INITIAL_CONFIGURE");
        return 3;
    }
    printf("S12_EGL_WINDOW_SIZE=%dx%d\n",c.width,c.height);
    struct wl_egl_window *win=wl_egl_window_create(c.surface,c.width,c.height);
    if(!win) {puts("S12_WAYLAND_EGL=NO_WL_EGL_WINDOW");return 3;}
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform=
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay egl=platform?
        platform(EGL_PLATFORM_WAYLAND_EXT,(void*)c.display,NULL):
        EGL_NO_DISPLAY;
    EGLint major=0,minor=0;
    if(egl==EGL_NO_DISPLAY||!eglInitialize(egl,&major,&minor)) {
        printf("S12_EGL_INITIALIZE=FAIL 0x%x\n",eglGetError());
        return 4;
    }
    if(getenv("ALROOT_S12_PRIVATE_EGL")) {
        int priv=private_egl_library_mapped();
        printf("S12_PRIVATE_EGL_MAPPED=%d\n",priv);
        if(!priv) {
            puts("S12_PRIVATE_EGL_MISMATCH=installed_EGL_was_used");
            return 6;
        }
    }
    printf("S12_EGL_VENDOR=%s\n",eglQueryString(egl,EGL_VENDOR));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
                    EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
                    EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig cfg=NULL;
    EGLint n=0;
    if(!eglChooseConfig(egl,attrs,&cfg,1,&n)||n<1) {
        printf("S12_EGL_WINDOW_CONFIG=FAIL 0x%x\n",eglGetError());
        return 4;
    }
    EGLint selected_visual=0;
    eglGetConfigAttrib(egl,cfg,EGL_NATIVE_VISUAL_ID,&selected_visual);
    printf("S12_EGL_NATIVE_VISUAL_ID=0x%08x\n",(unsigned)selected_visual);
    if(!eglBindAPI(EGL_OPENGL_ES_API)) return 4;
    EGLSurface s=eglCreateWindowSurface(egl,cfg,(EGLNativeWindowType)win,NULL);
    if(s==EGL_NO_SURFACE) {
        printf("S12_EGL_CREATE_WINDOW_SURFACE=FAIL 0x%x\n",eglGetError());
        return 4;
    }
    EGLint cattrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext(egl,cfg,EGL_NO_CONTEXT,cattrs);
    if(ctx==EGL_NO_CONTEXT||!eglMakeCurrent(egl,s,s,ctx)) {
        printf("S12_EGL_MAKE_CURRENT=FAIL 0x%x\n",eglGetError());
        return 4;
    }
    printf("S12_GLES_VENDOR=%s\n",glGetString(GL_VENDOR));
    printf("S12_GLES_RENDERER=%s\n",glGetString(GL_RENDERER));
    printf("S12_GLES_VERSION=%s\n",glGetString(GL_VERSION));
    fflush(stdout);
    int swapped=0;
    for(int i=0;i<frames&&!c.closed;i++) {
        /* Every frame changes color, so a static texture cannot pass. */
        float f=(float)(i%60)/59.0f;
        glViewport(0,0,c.width,c.height);
        glClearColor(f,0.24f,1.f-f,1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if(glGetError()!=GL_NO_ERROR) {
            printf("S12_GL_RENDER=ERROR_AT_%d\n",i);
            break;
        }
        if(!eglSwapBuffers(egl,s)) {
            printf("S12_EGL_SWAP=FAIL_AT_%d error=0x%x\n",i,eglGetError());
            break;
        }
        swapped++;
        if(wl_display_dispatch_pending(c.display)<0) break;
    }
    printf("S12_EGL_SWAP_SUCCESS_COUNT=%d\n",swapped);
    printf("S12_EGL_WAYLAND_SWAP=%s\n",swapped==frames?"PASS":"INCOMPLETE");
    fflush(stdout);
    eglMakeCurrent(egl,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(ctx!=EGL_NO_CONTEXT)eglDestroyContext(egl,ctx);
    eglDestroySurface(egl,s);
    eglTerminate(egl);
    wl_egl_window_destroy(win);
    xdg_toplevel_destroy(c.toplevel);
    xdg_surface_destroy(c.xdg_surface);
    wl_surface_destroy(c.surface);
    xdg_wm_base_destroy(c.wm);
    wl_compositor_destroy(c.compositor);
    wl_registry_destroy(reg);
    wl_display_disconnect(c.display);
    return swapped==frames?0:5;
}
