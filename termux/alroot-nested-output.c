/*
 * Alroot-only Wayland nested output mode setter.
 * Uses wlroots wlr-output-management-v1 (protocol copyright in XML).
 * Never touches physical Android output, original Termux-X11 or :0.
 *
 * Usage: WAYLAND_DISPLAY=wayland-1 alroot-nested-output 1920 1080
 * Do not request refresh: wlroots nested Wayland backend rejects nonzero Hz.
 */
#define _POSIX_C_SOURCE 200809L
#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <poll.h>
#include <time.h>
#include "wlr-output-management-client.h"

struct output_data {
    struct wl_display *display;
    struct zwlr_output_manager_v1 *manager;
    struct zwlr_output_head_v1 *head;
    uint32_t serial;
    bool ready;
    bool done;
    bool success;
    bool failed;
    char name[64];
};

static void head_name(void *userdata, struct zwlr_output_head_v1 *head,
                      const char *name) {
    struct output_data *data = userdata;
    if (data->head == head)
        snprintf(data->name, sizeof(data->name), "%s", name);
}
// Every Wayland event advertised by this interface must have a callback,
// even when this utility only needs its name and current configuration.
static void head_text(void *data,struct zwlr_output_head_v1 *head,const char *str)
    {(void)data;(void)head;(void)str;}
static void head_size(void *data,struct zwlr_output_head_v1 *head,int32_t w,int32_t h)
    {(void)data;(void)head;(void)w;(void)h;}
static void mode_size(void *data,struct zwlr_output_mode_v1 *mode,int32_t w,int32_t h)
    {(void)data;(void)mode;(void)w;(void)h;}
static void mode_refresh(void *data,struct zwlr_output_mode_v1 *mode,int32_t r)
    {(void)data;(void)mode;(void)r;}
static void mode_preferred(void *data,struct zwlr_output_mode_v1 *mode)
    {(void)data;(void)mode;}
static void mode_finished(void *data,struct zwlr_output_mode_v1 *mode)
    {(void)data;zwlr_output_mode_v1_destroy(mode);}
static const struct zwlr_output_mode_v1_listener mode_listener = {
    .size=mode_size,.refresh=mode_refresh,
    .preferred=mode_preferred,.finished=mode_finished,
};
static void head_mode(void *data,struct zwlr_output_head_v1 *head,
                      struct zwlr_output_mode_v1 *mode)
    {(void)head;zwlr_output_mode_v1_add_listener(mode,&mode_listener,data);}
static void head_enabled(void *data,struct zwlr_output_head_v1 *head,int32_t enabled)
    {(void)data;(void)head;(void)enabled;}
static void head_current_mode(void *data,struct zwlr_output_head_v1 *head,
                              struct zwlr_output_mode_v1 *mode)
    {(void)data;(void)head;(void)mode;}
static void head_position(void *data,struct zwlr_output_head_v1 *head,int32_t x,int32_t y)
    {(void)data;(void)head;(void)x;(void)y;}
static void head_transform(void *data,struct zwlr_output_head_v1 *head,int32_t v)
    {(void)data;(void)head;(void)v;}
static void head_scale(void *data,struct zwlr_output_head_v1 *head,wl_fixed_t v)
    {(void)data;(void)head;(void)v;}
static void head_finished(void *data,struct zwlr_output_head_v1 *head)
    {(void)data;(void)head;}
static void head_adaptive(void *data,struct zwlr_output_head_v1 *head,uint32_t v)
    {(void)data;(void)head;(void)v;}
static const struct zwlr_output_head_v1_listener head_listener = {
    .name=head_name,
    .description=head_text,.physical_size=head_size,.mode=head_mode,
    .enabled=head_enabled,.current_mode=head_current_mode,
    .position=head_position,.transform=head_transform,.scale=head_scale,
    .finished=head_finished,.make=head_text,.model=head_text,
    .serial_number=head_text,.adaptive_sync=head_adaptive,
};
static void manager_head(void *userdata,
        struct zwlr_output_manager_v1 *manager,
        struct zwlr_output_head_v1 *head) {
    struct output_data *data=userdata;
    if (data->head) {
        fprintf(stderr, "Multiple nested outputs: refusing ambiguous resize\n");
        data->failed = true;
        return;
    }
    data->head=head;
    zwlr_output_head_v1_add_listener(head,&head_listener,data);
}
static void manager_done(void *userdata,
        struct zwlr_output_manager_v1 *manager,uint32_t serial) {
    struct output_data *data=userdata;
    data->serial=serial;
    data->ready=true;
}
static void manager_finished(void *userdata,
        struct zwlr_output_manager_v1 *manager) {
    ((struct output_data*)userdata)->failed=true;
}
static const struct zwlr_output_manager_v1_listener manager_listener = {
    .head=manager_head,.done=manager_done,.finished=manager_finished,
};
static void global(void *userdata,struct wl_registry *registry,
                   uint32_t name,const char *interface,uint32_t version) {
    struct output_data *data=userdata;
    if (strcmp(interface,zwlr_output_manager_v1_interface.name)==0) {
        data->manager=wl_registry_bind(registry,name,
                &zwlr_output_manager_v1_interface,version<4?version:4);
        zwlr_output_manager_v1_add_listener(data->manager,
                                           &manager_listener,data);
    }
}
static void global_remove(void *userdata,struct wl_registry *registry,
                          uint32_t name) {(void)userdata;(void)registry;(void)name;}
static const struct wl_registry_listener registry_listener = {
    .global=global,.global_remove=global_remove,
};
static void config_succeeded(void *userdata,
                    struct zwlr_output_configuration_v1 *config) {
    struct output_data *data=userdata;
    data->success=true;
    data->done=true;
}
static void config_failed(void *userdata,
                    struct zwlr_output_configuration_v1 *config) {
    struct output_data *data=userdata;
    data->failed=true;
    data->done=true;
}
static void config_cancelled(void *userdata,
                    struct zwlr_output_configuration_v1 *config) {
    struct output_data *data=userdata;
    data->failed=true;
    data->done=true;
}
static const struct zwlr_output_configuration_v1_listener config_listener = {
    .succeeded=config_succeeded,.failed=config_failed,.cancelled=config_cancelled,
};

int main(int argc,char **argv) {
    if (argc!=3) {
        fprintf(stderr, "Usage: alroot-nested-output WIDTH HEIGHT (private Labwc Wayland socket)\n");
        return 2;
    }
    char extra=0;
    int w=0,h=0;
    if (sscanf(argv[1],"%d%c",&w,&extra)!=1 ||
        sscanf(argv[2],"%d%c",&h,&extra)!=1 ||
        w<320 || w>3840 || h<240 || h>2160) return 2;
    const char *socket=getenv("WAYLAND_DISPLAY");
    if (!socket || !strcmp(socket,"wayland-0") || !strcmp(socket,":0")) {
        fprintf(stderr,"Refusing native host or missing private nested Wayland socket\n");
        return 2;
    }
    struct output_data data={0};
    data.display=wl_display_connect(NULL);
    if (!data.display) return 3;
    struct wl_registry *registry=wl_display_get_registry(data.display);
    wl_registry_add_listener(registry,&registry_listener,&data);
    if (wl_display_roundtrip(data.display)<0 ||
        wl_display_roundtrip(data.display)<0 ||
        !data.manager || !data.ready || !data.head ||
        data.failed || strcmp(data.name,"WL-1")!=0) {
        fprintf(stderr,"Private Labwc output-management unavailable or output not WL-1\n");
        wl_display_disconnect(data.display);
        return 3;
    }
    struct zwlr_output_configuration_v1 *config=
        zwlr_output_manager_v1_create_configuration(data.manager,data.serial);
    if (!config) return 3;
    zwlr_output_configuration_v1_add_listener(config,&config_listener,&data);
    struct zwlr_output_configuration_head_v1 *confhead=
        zwlr_output_configuration_v1_enable_head(config,data.head);
    if (!confhead) return 3;
    // Important: Wayland nested wlroots backend supports custom mode
    // width/height, but non-zero refresh is rejected by backend test.
    zwlr_output_configuration_head_v1_set_custom_mode(confhead,w,h,0);
    zwlr_output_configuration_v1_apply(config);
    wl_display_flush(data.display);
    // Bound startup to 4s. Never loop forever if the session is broken.
    for (int n=0;n<40 && !data.done;n++) {
        struct pollfd p={.fd=wl_display_get_fd(data.display),.events=POLLIN};
        if (poll(&p,1,100)<0) break;
        if (p.revents & POLLIN) {
            if (wl_display_dispatch(data.display)<0) break;
        }
    }
    int rc=data.done && data.success ? 0 : 4;
    if (!rc) printf("Alroot nested Labwc WL-1 custom mode %dx%d applied\n",w,h);
    else fprintf(stderr,"Could not change private nested Labwc WL-1 mode to %dx%d\n",w,h);
    zwlr_output_configuration_v1_destroy(config);
    wl_display_disconnect(data.display);
    return rc;
}
