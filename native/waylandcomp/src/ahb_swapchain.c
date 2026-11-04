#define _GNU_SOURCE
#include "ahb_swapchain.h"
#include "alroot_color.h"
#include "alroot_ext.h"
#include "sc_layer.h"
#include "vk_present.h"
#include "banner-ahb-v1-server-protocol.h"
#include <android/hardware_buffer.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MOD_QCOM_COMPRESSED 0x0500000000000001ULL

struct alroot_dma_buf_sync_file { uint32_t flags; int32_t fd; };
#define ALROOT_DMA_BUF_SYNC_READ 1u
#define ALROOT_DMA_BUF_IOCTL_EXPORT_SYNC_FILE _IOWR('b', 2, struct alroot_dma_buf_sync_file)
#define ALROOT_DMA_BUF_IOCTL_IMPORT_SYNC_FILE _IOW('b', 3, struct alroot_dma_buf_sync_file)

extern volatile int g_zero_copy;

struct ahb_buf {
    uint64_t id;
    struct dmabuf_buffer *b;
    AHardwareBuffer *ahb;
    int w, h;
    uint32_t stride, image_count;
    uint32_t format;
    uint64_t modifier;
    struct wl_client *client;
    struct wl_resource *resource;
    struct wl_listener resource_destroy;
    int on_layer;
    int release_failed; /* never signal reuse when Android completion is unproven */
    int release_pending;
    int64_t deferred_ns;
    struct surface *surface;
    struct wl_event_source *fence_src;
    int fence_fd;
    struct wl_list link;
};

static struct wl_list g_bufs;
static struct wl_list g_clients;
static uint64_t g_next_id = 1;
static struct wl_event_loop *g_loop;
static unsigned g_stat_zero_copy;
static int g_export_failed_logged, g_import_failed_logged;
/* An available AHB GPU-copy importer is NOT proof of direct display layers. */
static int g_gpu_copy_available, g_layer_advertised;
static int g_mode_sent = -1;
static struct surface *g_announced;
static volatile int64_t g_last_zero_copy_ns;

struct released { uint64_t id; int fd; };
static pthread_mutex_t g_rel_lock = PTHREAD_MUTEX_INITIALIZER;
static struct released *g_rel;
static int g_rel_n, g_rel_cap;
static int g_rel_pipe[2] = {-1, -1};

struct chain_seen { struct wl_client *client; int w, h; uint32_t image_count; uint64_t modifier; uint32_t format; };
static struct chain_seen g_last_chain;

static int64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static struct ahb_buf *find_buf(uint64_t id) {
    struct ahb_buf *ab;
    wl_list_for_each(ab, &g_bufs, link)
        if (ab->id == id) return ab;
    return NULL;
}

static void buf_free(struct ahb_buf *ab) {
    if (ab->resource) { wl_list_remove(&ab->resource_destroy.link); ab->resource = NULL; }
    if (ab->fence_src) { wl_event_source_remove(ab->fence_src); ab->fence_src = NULL; }
    if (ab->fence_fd >= 0) { close(ab->fence_fd); ab->fence_fd = -1; }
    wl_list_remove(&ab->link);
    *alroot_dmabuf_ahb_slot(ab->b) = NULL;
    AHardwareBuffer_release(ab->ahb);
    alroot_dmabuf_unref(ab->b);
    free(ab);
}

static void maybe_free(struct ahb_buf *ab) {
    if (!ab->resource && !ab->on_layer && !ab->fence_src) buf_free(ab);
}

static void on_buffer_resource_destroyed(struct wl_listener *l, void *data) {
    struct ahb_buf *ab = wl_container_of(l, ab, resource_destroy);
    wl_list_remove(&ab->resource_destroy.link);
    ab->resource = NULL;
    ab->release_pending = 0;
    maybe_free(ab);
}


static void send_deferred_release(struct ahb_buf *ab) {
    if (ab->release_pending && ab->resource)
        alroot_release_buffer(ab->surface, ab->resource, ab->surface != NULL, ab->deferred_ns);
    ab->release_pending = 0;
    ab->surface = NULL;
}

static void release_failed(struct ahb_buf *ab,const char *where) {
    ab->release_failed=1;ab->on_layer=0;
    if(ab->resource)wl_client_post_implementation_error(wl_resource_get_client(ab->resource),
        "Android layer release invalid: %s",where);
    maybe_free(ab);
}
static int on_fence_readable(int fd,uint32_t mask,void *data) {
    struct ahb_buf *ab=data;
    int state=vkp_release_fd_state(fd);
    if(!state && !(mask&(WL_EVENT_ERROR|WL_EVENT_HANGUP)))return 0;
    wl_event_source_remove(ab->fence_src);ab->fence_src=NULL;
    close(ab->fence_fd);ab->fence_fd=-1;
    if(state<=0){release_failed(ab,"sync_file status");return 0;}
    ab->on_layer=0;send_deferred_release(ab);maybe_free(ab);return 0;
}
static void handle_released(uint64_t id,int fd) {
    struct ahb_buf *ab=find_buf(id);
    if(!ab){if(fd>=0)close(fd);return;}
    if(!ab->on_layer || ab->fence_src || ab->release_failed){
        if(fd>=0)close(fd);return; /* stale notification cannot release a second use */
    }
    if(fd < -1){release_failed(ab,"unexpected release payload");return;}
    if(fd>=0){
        int state=vkp_release_fd_state(fd);
        if(state<0){close(fd);release_failed(ab,"invalid release descriptor");return;}
        if(!state){
            struct alroot_dma_buf_sync_file imp={.flags=ALROOT_DMA_BUF_SYNC_READ,.fd=fd};
            int d=alroot_dmabuf_fd(ab->b);
            if(d<0 || ioctl(d,ALROOT_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&imp)!=0){
                if(!g_import_failed_logged){g_import_failed_logged=1;alroot_log("layer","release fence deferred on the Wayland event loop");}
                ab->fence_fd=fd;
                ab->fence_src=wl_event_loop_add_fd(g_loop,fd,WL_EVENT_READABLE,on_fence_readable,ab);
                if(!ab->fence_src){ab->fence_fd=-1;close(fd);release_failed(ab,"event watch");}
                return;
            }
        }
        close(fd);
    }
    ab->on_layer=0;send_deferred_release(ab);maybe_free(ab);
}

static int on_release_pipe(int fd, uint32_t mask, void *data) {
    char buf[64];
    while (read(fd, buf, sizeof(buf)) > 0) {}
    /* Swap the producer batch, then process it in FIFO order without the
     * Binder mutex. A burst used to memmove the remainder for every fd. */
    static struct released *spare;
    static int spare_cap;
    for (;;) {
        pthread_mutex_lock(&g_rel_lock);
        int count=g_rel_n,cap=g_rel_cap;
        if(!count){pthread_mutex_unlock(&g_rel_lock);break;}
        struct released *batch=g_rel;
        g_rel=spare;g_rel_cap=spare_cap;g_rel_n=0;
        pthread_mutex_unlock(&g_rel_lock);
        for(int i=0;i<count;i++)handle_released(batch[i].id,batch[i].fd);
        spare=batch;spare_cap=cap;
    }
    return 0;
}

void ahb_swapchain_layer_released(void *token, int release_fd) {
    pthread_mutex_lock(&g_rel_lock);
    if (g_rel_n == g_rel_cap) {
        int cap = g_rel_cap ? g_rel_cap * 2 : 16;
        struct released *n = realloc(g_rel, (size_t)cap * sizeof(*n));
        if (!n) { pthread_mutex_unlock(&g_rel_lock); if (release_fd >= 0) close(release_fd); return; }
        g_rel = n; g_rel_cap = cap;
    }
    g_rel[g_rel_n++] = (struct released){(uint64_t)(uintptr_t)token, release_fd};
    pthread_mutex_unlock(&g_rel_lock);
    if (g_rel_pipe[1] >= 0) { char c=1;ssize_t n;do{n=write(g_rel_pipe[1],&c,1);}while(n<0 && errno==EINTR); }
}


int ahb_swapchain_has_ahb(const struct dmabuf_buffer *b) {
    return b && *alroot_dmabuf_ahb_slot((struct dmabuf_buffer *)b) != NULL;
}

AHardwareBuffer *ahb_swapchain_borrow_ahb(const struct dmabuf_buffer *b) {
    struct ahb_buf *ab = b ? *alroot_dmabuf_ahb_slot((struct dmabuf_buffer *)b) : NULL;
    return ab ? ab->ahb : NULL;
}

int ahb_swapchain_present(struct dmabuf_buffer *b, struct surface *s, int scene_w, int scene_h) {
    struct ahb_buf *ab = b ? *alroot_dmabuf_ahb_slot(b) : NULL;
    if (!ab) return -1;
    if(ab->release_failed || ab->fence_src ||
       (ab->on_layer && !sc_layer_token_is_current((void *)(uintptr_t)ab->id)))return VKP_RENDER_RETRY;
    int dmabuf_fd = alroot_dmabuf_fd(b);
    if(dmabuf_fd<0)return -1; /* no implicit producer proof for this v1 path */
    int acquire = -1;
    struct alroot_dma_buf_sync_file exp = {.flags = ALROOT_DMA_BUF_SYNC_READ, .fd = -1};
    if (dmabuf_fd >= 0 && ioctl(dmabuf_fd, ALROOT_DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp) == 0 && exp.fd >= 0) {
        acquire = exp.fd;
    } else if (dmabuf_fd >= 0) {
        if (!g_export_failed_logged) {
            g_export_failed_logged = 1;
            alroot_log("layer", "zero-copy: DMA_BUF_IOCTL_EXPORT_SYNC_FILE failed (%s): deferring until dma-buf is readable",
                       strerror(errno));
        }
        struct pollfd p = {.fd = dmabuf_fd, .events = POLLIN};
        int r;
        do { r = poll(&p, 1, 0); } while (r < 0 && errno == EINTR);
        if(!r)return VKP_RENDER_RETRY;
        if(r<0 || !(p.revents&POLLIN) || (p.revents&(POLLERR|POLLHUP|POLLNVAL))) {
            if(!ab->release_failed){release_failed(ab,"invalid implicit acquire fd");}
            return VKP_RENDER_RETRY;
        }
    }
    int r = sc_layer_present_ahb(ab->ahb, ab->w, ab->h, acquire, (void *)(uintptr_t)ab->id, scene_w, scene_h,
                                 s ? alroot_surface_color(s) : NULL, ab->format);
    if (r < 0) return -1;
    if (r == 1) return 0;
    if (!ab->on_layer) {
        ab->on_layer = 1;
        g_stat_zero_copy++;
        g_last_zero_copy_ns = now_ns();
    }
    if (s && g_announced != s) {
        char name[160];
        g_announced = s;
        alroot_surface_describe(s, name, sizeof(name));
        alroot_log("layer", "zero-copy: presenting %s without a copy", name);
    }
    return 0;
}

int ahb_swapchain_last_frame_age_ms(void) {
    int64_t t = g_last_zero_copy_ns;
    if (!t) return -1;
    int64_t age = (now_ns() - t) / 1000000LL;
    return age > INT32_MAX ? INT32_MAX : (int)age;
}

int ahb_swapchain_defer_release(struct dmabuf_buffer *b, struct wl_resource *buffer, struct surface *s, int paced) {
    struct ahb_buf *ab = b ? *alroot_dmabuf_ahb_slot(b) : NULL;
    if (!ab || !buffer || ab->resource != buffer) return 0;
    if (ab->release_failed) return 1;
    if (!ab->on_layer) return 0;
    ab->release_pending = 1;
    ab->deferred_ns = now_ns();
    ab->surface = paced ? s : NULL;
    return 1;
}

void ahb_swapchain_surface_gone(struct surface *s) {
    struct ahb_buf *ab;
    wl_list_for_each(ab, &g_bufs, link)
        if (ab->surface == s) ab->surface = NULL;
}

unsigned ahb_swapchain_stats_take(void) {
    unsigned n = g_stat_zero_copy;
    g_stat_zero_copy = 0;
    return n;
}

/* Keep the pre-v3 meaning used by the existing HDR direct-layer gate. */
int ahb_swapchain_advertised(void) { return g_layer_advertised; }

uint32_t ahb_swapchain_ahb_format(const struct dmabuf_buffer *b) {
    struct ahb_buf *ab = b ? *alroot_dmabuf_ahb_slot((struct dmabuf_buffer *)b) : NULL;
    return ab ? ab->format : 0;
}


static void ahb_destroy(struct wl_client *c, struct wl_resource *r) { wl_resource_destroy(r); }

static void ahb_attach(struct wl_client *c, struct wl_resource *r, struct wl_resource *buffer, int32_t sock,
                       uint32_t width, uint32_t height, uint32_t stride, uint32_t mod_hi, uint32_t mod_lo,
                       uint32_t image_count) {
    struct dmabuf_buffer *b = alroot_dmabuf_from_resource(buffer);
    uint64_t modifier = ((uint64_t)mod_hi << 32) | mod_lo;
    if (!b) {
        alroot_log("layer", "zero-copy: %s attached an AHardwareBuffer to a non-dma-buf wl_buffer, ignored", alroot_client_name(c));
        close(sock);
        return;
    }
    void **slot = alroot_dmabuf_ahb_slot(b);
    if (*slot) { close(sock); return; }

    struct pollfd p = {.fd = sock, .events = POLLIN};
    int pr;
    do { pr = poll(&p, 1, 1000); } while (pr < 0 && errno == EINTR);
    AHardwareBuffer *ahb = NULL;
    int rc = pr > 0 ? AHardwareBuffer_recvHandleFromUnixSocket(sock, &ahb) : -1;
    close(sock);
    if (rc != 0 || !ahb) {
        alroot_log("layer", "zero-copy: receiving %s's AHardwareBuffer failed (%s)", alroot_client_name(c),
                   pr > 0 ? "recvHandleFromUnixSocket" : "nothing on the socket");
        return;
    }
    AHardwareBuffer_Desc d;
    AHardwareBuffer_describe(ahb, &d);
    int bw, bh;
    alroot_dmabuf_size(b, &bw, &bh);
    if ((int)d.width != bw || (int)d.height != bh || (int)width != bw || (int)height != bh) {
        alroot_log("layer", "zero-copy: %s's AHardwareBuffer is %ux%u but its wl_buffer %dx%d, ignored",
                   alroot_client_name(c), d.width, d.height, bw, bh);
        AHardwareBuffer_release(ahb);
        return;
    }

    struct ahb_buf *ab = calloc(1, sizeof(*ab));
    if (!ab) { AHardwareBuffer_release(ahb); wl_client_post_no_memory(c); return; }
    ab->id = g_next_id++;
    ab->b = b;
    alroot_dmabuf_ref(b);
    ab->ahb = ahb;
    ab->w = bw; ab->h = bh;
    ab->stride = d.stride ? d.stride : stride;
    ab->image_count = image_count;
    ab->format = d.format;
    ab->modifier = modifier;
    ab->client = c;
    ab->resource = buffer;
    ab->resource_destroy.notify = on_buffer_resource_destroyed;
    wl_resource_add_destroy_listener(buffer, &ab->resource_destroy);
    ab->fence_fd = -1;
    wl_list_insert(g_bufs.prev, &ab->link);
    *slot = ab;

    if (g_last_chain.client != c || g_last_chain.w != bw || g_last_chain.h != bh ||
        g_last_chain.image_count != image_count || g_last_chain.modifier != modifier || g_last_chain.format != d.format) {
        g_last_chain = (struct chain_seen){c, bw, bh, image_count, modifier, d.format};
        alroot_log("layer", "zero-copy: AHB swapchain from %s (%u images, %dx%d, %s, %s, stride %u px)", alroot_client_name(c),
                   image_count, bw, bh, alroot_ahb_format_name(d.format), modifier == MOD_QCOM_COMPRESSED ? "UBWC (QCOM_COMPRESSED)"
                                       : modifier == 0 ? "linear" : "unknown modifier", ab->stride);
    }
}

static const struct banner_ahb_v1_interface ahb_impl = {
    .destroy = ahb_destroy,
    .attach = ahb_attach,
};

static void on_client_resource_destroyed(struct wl_resource *r) {
    wl_list_remove(wl_resource_get_link(r));
}

static void bind_ahb(struct wl_client *c, void *data, uint32_t ver, uint32_t id) {
    static struct wl_client *last_named;
    if (ver > 3) ver = 3;
    struct wl_resource *r = wl_resource_create(c, &banner_ahb_v1_interface, (int)ver, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &ahb_impl, NULL, on_client_resource_destroyed);
    wl_list_insert(&g_clients, wl_resource_get_link(r));
    if (ver >= BANNER_AHB_V1_MODE_SINCE_VERSION)
        banner_ahb_v1_send_mode(r, g_zero_copy && g_layer_advertised ? 1u : 0u);
    if (ver >= BANNER_AHB_V1_GPU_CAPABILITIES_SINCE_VERSION)
        banner_ahb_v1_send_gpu_capabilities(r,
                (g_gpu_copy_available ? 1u : 0u) | (g_layer_advertised ? 2u : 0u));
    if (last_named != c) {
        last_named = c;
        alroot_log("layer", "zero-copy: %s bound banner_ahb_v1 version %u (%s)", alroot_client_name(c), ver,
                   ver >= 2 ? "follows the live switch" : "version 1: decides from its launch environment only");
    }
}

void ahb_swapchain_set_mode(int on, int live) {
    /* The legacy mode event controls ONLY direct layers, not Stage-1 copy. */
    on = on && g_layer_advertised ? 1 : 0;
    g_zero_copy = on;
    if (!g_layer_advertised) {
        if (live) alroot_log("layer", "zero-copy switched %s from the drawer, but no display layer is available on this device: no change",
                             on ? "on" : "off");
        return;
    }
    if (g_mode_sent == on) return;
    g_mode_sent = on;
    int told = 0;
    struct wl_resource *r;
    wl_resource_for_each(r, &g_clients) {
        if (wl_resource_get_version(r) >= BANNER_AHB_V1_MODE_SINCE_VERSION) {
            banner_ahb_v1_send_mode(r, (uint32_t)on);
            told++;
        }
    }
    memset(&g_last_chain, 0, sizeof(g_last_chain));
    g_announced = NULL;
    if (live)
        alroot_log("layer", "zero-copy switched %s from the drawer: %d bound program%s told to rebuild their swapchains%s",
                   on ? "on" : "off", told, told == 1 ? "" : "s",
                   on ? "; frames go on the display layer once the new gralloc swapchain is up"
                      : "; gralloc frames still in flight stay on the layer (or take the copy path) until then");
    else
        alroot_log("layer", "zero-copy: %s at launch (%s)", on ? "on" : "off",
                   on ? "HDR session: games present their own gralloc buffers on the display layer"
                      : "only HDR sessions turn it on");
    if (live) wl_display_flush_clients(alroot_get_display());
    alroot_request_redraw();
}

void ahb_swapchain_init(struct wl_display *display) {
    wl_list_init(&g_bufs);
    wl_list_init(&g_clients);
    g_gpu_copy_available = vkp_ahb_gpu_copy_supported() ? 1 : 0;
    const int layer_candidate = sc_layer_available() ? 1 : 0;
    if (!g_gpu_copy_available && !layer_candidate) {
        alroot_log("layer", "AHB unsupported: no host Vulkan GPU-copy import and no direct layers");
        return;
    }
    g_loop = wl_display_get_event_loop(display);
    if (layer_candidate) {
        if (pipe2(g_rel_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
            alroot_log("error", "zero-copy: release pipe creation failed (%s); direct layers disabled",
                       strerror(errno));
            g_rel_pipe[0] = g_rel_pipe[1] = -1;
        } else {
            wl_event_loop_add_fd(g_loop, g_rel_pipe[0], WL_EVENT_READABLE, on_release_pipe, NULL);
            g_layer_advertised = 1;
        }
    }
    if (!g_gpu_copy_available && !g_layer_advertised) return;
    if (!wl_global_create(display, &banner_ahb_v1_interface, 3, NULL, bind_ahb)) {
        alroot_log("error", "AHB: banner_ahb_v1 global creation failed");
        g_layer_advertised = 0;
        return;
    }
    alroot_log("layer", "banner_ahb_v1 version 3: AHB GPU-copy importer=%s, direct layer=%s; "
               "v2 mode semantics retained",
               g_gpu_copy_available ? "candidate" : "unavailable",
               g_layer_advertised ? "available" : "unavailable");
    ahb_swapchain_set_mode(g_zero_copy, 0);
}
