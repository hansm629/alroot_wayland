#define _GNU_SOURCE
#include "alroot_ahb_wayland_presenter.h"
#include "alroot-gpu-buffer-v1-client-protocol.h"
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define ALROOT_S12_MAX_SLOTS 32

struct ahb_slot {
    struct wl_buffer *buffer;
    AHardwareBuffer *ahb;
    uint32_t width, height;
    uint64_t last_used;
    struct alroot_ahb_wl_presenter *owner;
    int busy;
};

struct alroot_ahb_wl_presenter {
    struct wl_display *display;
    struct wl_surface *surface;
    struct wl_event_queue *queue;
    struct wl_registry *registry;
    struct alroot_gpu_buffer_v1 *manager;
    void *nativewindow;
    int (*send)(const AHardwareBuffer *, int);
    void (*acquire)(AHardwareBuffer *);
    void (*release)(AHardwareBuffer *);
    void (*describe)(const AHardwareBuffer *, AHardwareBuffer_Desc *);
    struct ahb_slot *slots[ALROOT_S12_MAX_SLOTS];
    uint32_t nslots;
    uint64_t serial;
    struct alroot_ahb_wl_stats stats;
};

static void on_global(void *data, struct wl_registry *registry,
                      uint32_t name, const char *interface, uint32_t version) {
    struct alroot_ahb_wl_presenter *p=data;
    if (version<1 || p->manager ||
        strcmp(interface,alroot_gpu_buffer_v1_interface.name))
        return;
    p->manager=wl_registry_bind(registry,name,&alroot_gpu_buffer_v1_interface,
                                version>=2?2:1);
    if (p->manager)
        wl_proxy_set_queue((struct wl_proxy *)p->manager,p->queue);
}
static void on_removed(void *data,struct wl_registry *registry,uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener registry_listener={
    .global=on_global,.global_remove=on_removed
};

static void on_release(void *data,struct wl_buffer *buffer) {
    struct ahb_slot *slot=data;
    if (slot->buffer != buffer || !slot->busy) return;
    slot->busy=0;
    slot->owner->stats.released++;
}
static const struct wl_buffer_listener buffer_listener={
    .release=on_release
};

static void drop_slot(struct alroot_ahb_wl_presenter *p,uint32_t index) {
    struct ahb_slot *slot=p->slots[index];
    if (!slot) return;
    if (slot->buffer) wl_buffer_destroy(slot->buffer);
    if (slot->ahb) p->release(slot->ahb);
    free(slot);
    p->slots[index]=NULL;
    if (p->stats.live_slots) p->stats.live_slots--;
}

struct alroot_ahb_wl_presenter *
alroot_ahb_wl_presenter_create(struct wl_display *display,
                              struct wl_surface *surface) {
    if (!display || !surface) return NULL;
    struct alroot_ahb_wl_presenter *p=calloc(1,sizeof(*p));
    if (!p) return NULL;
    p->display=display;
    p->surface=surface;
    p->nativewindow=dlopen("/system/lib64/libnativewindow.so",
                           RTLD_NOW|RTLD_LOCAL);
    if (!p->nativewindow) goto fail;
    *(void **)&p->send=dlsym(p->nativewindow,
                            "AHardwareBuffer_sendHandleToUnixSocket");
    *(void **)&p->acquire=dlsym(p->nativewindow,"AHardwareBuffer_acquire");
    *(void **)&p->release=dlsym(p->nativewindow,"AHardwareBuffer_release");
    *(void **)&p->describe=dlsym(p->nativewindow,"AHardwareBuffer_describe");
    if (!p->send || !p->acquire || !p->release || !p->describe) goto fail;
    p->queue=wl_display_create_queue_with_name(display,"alroot s1.2 AHB release queue");
    if (!p->queue) goto fail;
    p->registry=wl_display_get_registry(display);
    if (!p->registry) goto fail;
    wl_proxy_set_queue((struct wl_proxy *)p->registry,p->queue);
    if (wl_registry_add_listener(p->registry,&registry_listener,p)<0) goto fail;
    if (wl_display_roundtrip_queue(display,p->queue)<0 || !p->manager)
        goto fail;
    return p;
fail:
    alroot_ahb_wl_presenter_destroy(p);
    return NULL;
}

int alroot_ahb_wl_presenter_dispatch(struct alroot_ahb_wl_presenter *p,
                                    int timeout_ms) {
    if (!p || timeout_ms<0) return -EINVAL;
    int n=wl_display_dispatch_queue_pending(p->display,p->queue);
    if(n<0 || timeout_ms==0) return n<0?-EPIPE:n;
    while(wl_display_prepare_read_queue(p->display,p->queue)!=0)
        if(wl_display_dispatch_queue_pending(p->display,p->queue)<0)
            return -EPIPE;
    if(wl_display_flush(p->display)<0 && errno!=EAGAIN) {
        wl_display_cancel_read(p->display);
        return -EPIPE;
    }
    struct pollfd pf={.fd=wl_display_get_fd(p->display),.events=POLLIN};
    int ready;
    do {ready=poll(&pf,1,timeout_ms);} while(ready<0&&errno==EINTR);
    if(ready>0 && (pf.revents&POLLIN)) {
        if(wl_display_read_events(p->display)<0) return -EPIPE;
    } else {
        wl_display_cancel_read(p->display);
        if(ready<0) return -errno;
    }
    n=wl_display_dispatch_queue_pending(p->display,p->queue);
    return n<0?-EPIPE:n;
}

static struct ahb_slot *
new_slot(struct alroot_ahb_wl_presenter *p,AHardwareBuffer *ahb,
         uint32_t width,uint32_t height,uint32_t format) {
    struct ahb_slot *slot=calloc(1,sizeof(*slot));
    if(!slot) return NULL;
    int sockets[2];
    if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets)<0) {
        free(slot); return NULL;
    }
    int sent=p->send(ahb,sockets[0]);
    close(sockets[0]);
    if(sent) {close(sockets[1]);free(slot);return NULL;}
    if(format==AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM) {
        slot->buffer=alroot_gpu_buffer_v1_create_buffer(p->manager,
                                                        sockets[1],width,height);
    } else if (format==ALROOT_GPU_BUFFER_V1_FORMAT_BGRA8888 &&
               alroot_gpu_buffer_v1_get_version(p->manager)>=2) {
        slot->buffer=alroot_gpu_buffer_v1_create_buffer_with_format(
            p->manager,sockets[1],width,height,format);
    }
    close(sockets[1]);
    if(!slot->buffer) {free(slot);return NULL;}
    slot->owner=p;
    slot->ahb=ahb;
    slot->width=width;
    slot->height=height;
    wl_proxy_set_queue((struct wl_proxy *)slot->buffer,p->queue);
    if(wl_buffer_add_listener(slot->buffer,&buffer_listener,slot)<0) {
        wl_buffer_destroy(slot->buffer);
        free(slot);
        return NULL;
    }
    p->acquire(ahb);
    p->stats.registered++;
    return slot;
}

int alroot_ahb_wl_presenter_is_available(struct alroot_ahb_wl_presenter *p,
                                         AHardwareBuffer *ahb) {
    if (!p || !ahb) return -EINVAL;
    if(alroot_ahb_wl_presenter_dispatch(p,0)<0) return -EPIPE;
    for(uint32_t i=0;i<p->nslots;i++)
        if(p->slots[i] && p->slots[i]->ahb==ahb)
            return !p->slots[i]->busy;
    return 1;
}

int alroot_ahb_wl_presenter_submit(struct alroot_ahb_wl_presenter *p,
                                   AHardwareBuffer *ahb,
                                   uint32_t width,uint32_t height) {
    if(!p || !ahb || !p->manager || !width || !height) return -EINVAL;
    if(alroot_ahb_wl_presenter_dispatch(p,0)<0) return -EPIPE;
    AHardwareBuffer_Desc desc={0};
    p->describe(ahb,&desc);
    if(desc.width!=width || desc.height!=height || desc.layers!=1 ||
       (desc.format!=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM &&
        !(desc.format==ALROOT_GPU_BUFFER_V1_FORMAT_BGRA8888 &&
          alroot_gpu_buffer_v1_get_version(p->manager)>=2)) ||
       !(desc.usage&AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE))
        return -ENOTSUP;
    struct ahb_slot *slot=NULL;
    for(uint32_t i=0;i<p->nslots;i++)
        if(p->slots[i] && p->slots[i]->ahb==ahb &&
           p->slots[i]->width==width && p->slots[i]->height==height) {
            slot=p->slots[i];break;
        }
    if(slot && slot->busy) {
        p->stats.busy_rejects++;
        return -EAGAIN;
    }
    if(slot) p->stats.reused++;
    else {
        uint32_t index=p->nslots;
        if(index>=ALROOT_S12_MAX_SLOTS) {
            index=ALROOT_S12_MAX_SLOTS;
            uint64_t oldest=UINT64_MAX;
            for(uint32_t i=0;i<p->nslots;i++)
                if(p->slots[i] && !p->slots[i]->busy &&
                   p->slots[i]->last_used<oldest) {
                    oldest=p->slots[i]->last_used;
                    index=i;
                }
            if(index==ALROOT_S12_MAX_SLOTS) {
                p->stats.busy_rejects++;
                return -EAGAIN;
            }
            drop_slot(p,index);
            p->stats.evicted++;
        } else p->nslots++;
        slot=new_slot(p,ahb,width,height,desc.format);
        if(!slot) return -ENOMEM;
        p->slots[index]=slot;
        p->stats.live_slots++;
    }
    slot->busy=1;
    slot->last_used=++p->serial;
    wl_surface_attach(p->surface,slot->buffer,0,0);
    wl_surface_damage_buffer(p->surface,0,0,(int32_t)width,(int32_t)height);
    wl_surface_commit(p->surface);
    if(wl_display_flush(p->display)<0 && errno!=EAGAIN) return -EPIPE;
    p->stats.submitted++;
    return 0;
}

struct alroot_ahb_wl_stats
alroot_ahb_wl_presenter_stats(const struct alroot_ahb_wl_presenter *p) {
    const struct alroot_ahb_wl_stats zero={0};
    return p?p->stats:zero;
}

void alroot_ahb_wl_presenter_destroy(struct alroot_ahb_wl_presenter *p) {
    if(!p)return;
    for(uint32_t i=0;i<p->nslots;i++)drop_slot(p,i);
    if(p->manager)alroot_gpu_buffer_v1_destroy(p->manager);
    if(p->registry)wl_registry_destroy(p->registry);
    if(p->display)wl_display_flush(p->display);
    if(p->queue)wl_event_queue_destroy(p->queue);
    if(p->nativewindow)dlclose(p->nativewindow);
    free(p);
}
