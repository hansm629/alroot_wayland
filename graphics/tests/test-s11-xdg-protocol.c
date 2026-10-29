/* Read-only XDG configure handshake test, no GPU buffer allocations.
 * Compares live Alroot's window sizing to the Stage-1.1 policy.
 * Other desktop environments use the same xdg_toplevel interface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

struct check {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct xdg_wm_base *shell;
    int width, height;
    int configured, fullscreen, maximized;
    unsigned configure_count;
};

static void on_global(void *data, struct wl_registry *registry,
                      uint32_t name, const char *iface, uint32_t ver) {
    struct check *c=data;
    if (!strcmp(iface,wl_compositor_interface.name))
        c->compositor=wl_registry_bind(registry,name,&wl_compositor_interface,
                                       ver<4?ver:4);
    if (!strcmp(iface,xdg_wm_base_interface.name))
        c->shell=wl_registry_bind(registry,name,&xdg_wm_base_interface,1);
}
static void on_remove(void *data, struct wl_registry *reg, uint32_t name) {
    (void)data; (void)reg; (void)name;
}
static const struct wl_registry_listener registry_listener={
    .global=on_global,.global_remove=on_remove
};
static void on_ping(void *data,struct xdg_wm_base *shell,uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(shell,serial);
}
static const struct xdg_wm_base_listener shell_listener={.ping=on_ping};
static void on_surface_configure(void *data,struct xdg_surface *surf,
                                 uint32_t serial) {
    struct check *c=data;
    xdg_surface_ack_configure(surf,serial);
    c->configured=1;
    c->configure_count++;
}
static const struct xdg_surface_listener surface_listener={
    .configure=on_surface_configure
};
static void on_toplevel_configure(void *data,struct xdg_toplevel *t,
                                  int32_t width,int32_t height,
                                  struct wl_array *states) {
    (void)t;
    struct check *c=data;
    c->width=width;
    c->height=height;
    c->fullscreen=0;
    c->maximized=0;
    uint32_t *state;
    wl_array_for_each(state,states) {
        if (*state==XDG_TOPLEVEL_STATE_FULLSCREEN) c->fullscreen=1;
        if (*state==XDG_TOPLEVEL_STATE_MAXIMIZED) c->maximized=1;
    }
}
static void on_close(void *data,struct xdg_toplevel *t) {
    (void)t;
    ((struct check *)data)->configured=-1;
}
static const struct xdg_toplevel_listener toplevel_listener={
    .configure=on_toplevel_configure,.close=on_close
};
static int verify(struct check *c,const char *phase,
                  int width,int height,int fullscreen,int maximized) {
    unsigned previous=c->configure_count;
    if (wl_display_roundtrip(c->display)<0)
        return -1;
    if (c->configure_count<=previous || c->width!=width ||
        c->height!=height || c->fullscreen!=fullscreen ||
        c->maximized!=maximized) {
        fprintf(stderr,"S11_XDG_%s=FAIL got=%dx%d full=%d max=%d configured=%u\n",
                phase,c->width,c->height,c->fullscreen,c->maximized,
                c->configure_count);
        return -1;
    }
    printf("S11_XDG_%s=PASS %dx%d full=%d max=%d\n",
           phase,c->width,c->height,c->fullscreen,c->maximized);
    return 0;
}

int main(void) {
    struct check c={0};
    c.display=wl_display_connect(NULL);
    if (!c.display) { puts("S11_XDG=NO_WAYLAND"); return 2; }
    struct wl_registry *reg=wl_display_get_registry(c.display);
    wl_registry_add_listener(reg,&registry_listener,&c);
    if (wl_display_roundtrip(c.display)<0 || !c.compositor || !c.shell)
        return 2;
    xdg_wm_base_add_listener(c.shell,&shell_listener,&c);
    struct wl_surface *surface=wl_compositor_create_surface(c.compositor);
    struct xdg_surface *xdg=xdg_wm_base_get_xdg_surface(c.shell,surface);
    xdg_surface_add_listener(xdg,&surface_listener,&c);
    struct xdg_toplevel *tl=xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(tl,&toplevel_listener,&c);
    xdg_toplevel_set_title(tl,"Alroot S1.1 XDG protocol validation");
    wl_surface_commit(surface);
    if (wl_display_roundtrip(c.display)<0 || !c.configured) return 2;
    const char *legacy=getenv("ALROOT_S11_ALLOW_LEGACY_FULLSCREEN_WINDOW");
    if (legacy && c.width>0 && c.height>0)
        printf("S11_XDG_LEGACY_WINDOWED=PASS %dx%d (existing P20 behavior)\n",
               c.width,c.height);
    else if (c.width!=0 || c.height!=0) {
        printf("S11_XDG_WINDOWED=FAIL got=%dx%d expected=0x0\n",
               c.width,c.height);
        return 3;
    } else puts("S11_XDG_WINDOWED=PASS 0x0 (client chooses buffer size)");

    /* Observe the actual server's output dimensions when requesting
     * fullscreen, without changing Android output size or XFCE settings. */
    xdg_toplevel_set_fullscreen(tl,NULL);
    if (wl_display_roundtrip(c.display)<0 || !c.fullscreen ||
        c.width<1 || c.height<1) return 3;
    const int output_w=c.width,output_h=c.height;
    printf("S11_XDG_FULLSCREEN=PASS %dx%d\n",output_w,output_h);

    xdg_toplevel_unset_fullscreen(tl);
    if (verify(&c,"UNSET_FULLSCREEN",
               legacy?output_w:0,legacy?output_h:0,0,0)!=0) return 3;
    xdg_toplevel_set_maximized(tl);
    if (legacy) {
        /* Old P20 ignores maximization requests; no status to compare. */
        puts("S11_XDG_MAXIMIZE=LEGACY_SKIPPED");
    } else if (verify(&c,"MAXIMIZED",output_w,output_h,0,1)!=0)
        return 3;
    xdg_toplevel_unset_maximized(tl);
    if (!legacy && verify(&c,"UNMAXIMIZED",0,0,0,0)!=0)
        return 3;
    xdg_toplevel_destroy(tl);
    xdg_surface_destroy(xdg);
    wl_surface_destroy(surface);
    xdg_wm_base_destroy(c.shell);
    wl_compositor_destroy(c.compositor);
    wl_registry_destroy(reg);
    wl_display_disconnect(c.display);
    puts("S11_XDG_POLICY=PASS");
    return 0;
}
