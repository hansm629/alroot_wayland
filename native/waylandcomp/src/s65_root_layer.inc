/* S65 presenter: called from the Wayland thread, except s65_buffer_released.
 * AHardwareBuffer identity is NOT a submit key. Android 36's callback is
 * associated with this exact setBufferWithRelease call, including dropped
 * frames. Nothing here assumes a transaction latch/completion releases a
 * buffer. The compositor separately pins its Wayland use until both producer
 * and consumer are complete. */
#define S65_MAX_RELEASES 16
enum s65_slot_state { S65_EMPTY, S65_OWNED_BY_ANDROID, S65_CALLBACK_DELIVERED, S65_ABANDONED };
struct s65_release_slot {
    enum s65_slot_state state;
    uint64_t serial;
    int fd;
    uint64_t trace_id;
};
static struct {
    struct layer root;
    ASurfaceControl *back;
    AHardwareBuffer *ahb; /* borrowed while host pins this active commit */
    uint64_t serial;
    int rect[8], ow, oh, bars;
} root65;
static struct s65_release_slot root65_slots[S65_MAX_RELEASES];
static struct sc_root_stats root65_stats;
static pthread_mutex_t root65_lock = PTHREAD_MUTEX_INITIALIZER;
static int root65_wake = -1; /* process lifetime: callbacks can outlive window loss */
/* Android retains only a nonzero serial in its callback context, not a
 * pointer to a reusable slot. If OnCommit never arrives, cancelling the
 * pacing notice frees the slot without waiting for or releasing any AHB. */
static struct s65_commit_slot {
    uint64_t serial;
    int64_t start_ns,callback_ns;
} root65_commits[S65_MAX_RELEASES];
static uint64_t root65_window_epoch=1;
static const char *root65_refusal="not_attempted";
uint64_t sc_root_window_epoch(void) { return root65_window_epoch; }
const char *sc_root_last_refusal(void) { return root65_refusal; }
static int s65_root_trace(void) {
    static int enabled=-1;
    if (enabled<0) { const char *s=getenv("S65_ROOT_TRACE"); enabled=s && !strcmp(s,"1"); }
    return enabled;
}

int sc_root_event_fd(void) {
    if (root65_wake < 0) root65_wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    return root65_wake;
}
static void s65_notify_loop(void) {
    uint64_t one=1;
    ssize_t n;
    do {n=write(root65_wake,&one,sizeof(one));} while(n<0 && errno==EINTR);
}
void sc_root_ack_events(void) {
    if(root65_wake<0) return;
    uint64_t counter; ssize_t n;
    do {n=read(root65_wake,&counter,sizeof(counter));} while(n<0 && errno==EINTR);
}
static int64_t s65_commit_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (int64_t)t.tv_sec*1000000000LL+t.tv_nsec;
}
#ifdef ALROOT_S66_ANDROID
#include "s66_transactions.inc"
#endif
static void s65_on_committed(void *opaque,ASurfaceTransactionStats *stats) {
    (void)stats; /* No release-fence or latch query on a commit notification. */
    const uint64_t token=(uint64_t)(uintptr_t)opaque;
    int notify=0;
    pthread_mutex_lock(&root65_lock);
    for(unsigned i=0;i<S65_MAX_RELEASES;i++) {
        struct s65_commit_slot *s=&root65_commits[i];
        if(s->serial!=token || s->callback_ns) continue;
        s->callback_ns=s65_commit_now();
        uint64_t latency=(uint64_t)(s->callback_ns-s->start_ns);
        root65_stats.commit_latency_ns+=latency;
        if(latency>root65_stats.commit_latency_max_ns) root65_stats.commit_latency_max_ns=latency;
        ++root65_stats.commit_arrived;notify=1;break;
    }
    if(!notify) ++root65_stats.commit_late;
    pthread_mutex_unlock(&root65_lock);
    if(notify)s65_notify_loop();
}
static int s65_arm_commit(ASurfaceTransaction *tx,uint64_t token) {
    int ok=0;
    pthread_mutex_lock(&root65_lock);
    if(api.setOnCommit && (uint64_t)(uintptr_t)token==token)
        for(unsigned i=0;i<S65_MAX_RELEASES;i++) if(!root65_commits[i].serial) {
            root65_commits[i]=(struct s65_commit_slot){token,s65_commit_now(),0};
            ++root65_stats.commit_armed;
            if(++root65_stats.commit_pending>root65_stats.commit_high_water)
                root65_stats.commit_high_water=root65_stats.commit_pending;
            ok=1;break;
        }
    if(!ok)++root65_stats.commit_unavailable;
    pthread_mutex_unlock(&root65_lock);
    if(ok)api.setOnCommit(tx,(void*)(uintptr_t)token,s65_on_committed);
    return ok;
}
int sc_root_take_commit(uint64_t *serial,int64_t *callback_mono_ns) {
    if(!serial || !callback_mono_ns) return 0;
    int got=0;
    pthread_mutex_lock(&root65_lock);
    for(unsigned i=0;i<S65_MAX_RELEASES;i++) {
        struct s65_commit_slot *s=&root65_commits[i];
        if(!s->serial || !s->callback_ns)continue;
        *serial=s->serial;*callback_mono_ns=s->callback_ns;
        s->serial=0;++root65_stats.commit_taken;--root65_stats.commit_pending;got=1;break;
    }
    pthread_mutex_unlock(&root65_lock);
    return got;
}
void sc_root_cancel_commit(uint64_t token) {
    if(!token)return;
    pthread_mutex_lock(&root65_lock);
    for(unsigned i=0;i<S65_MAX_RELEASES;i++)if(root65_commits[i].serial==token){
        root65_commits[i].serial=0;++root65_stats.commit_canceled;--root65_stats.commit_pending;break;
    }
    pthread_mutex_unlock(&root65_lock);
}
static void s65_buffer_released(void *opaque, int fd) {
    struct s65_release_slot *s = opaque;
    pthread_mutex_lock(&root65_lock);
    if (s->state == S65_ABANDONED) {
        s->state=S65_EMPTY; --root65_stats.outstanding;
        pthread_mutex_unlock(&root65_lock);
        if (fd >= 0) close(fd);
        return;
    }
    if (s->state != S65_OWNED_BY_ANDROID) {
        pthread_mutex_unlock(&root65_lock);
        if (fd >= 0) close(fd);
        return;
    }
    s->fd = fd;
    s->state = S65_CALLBACK_DELIVERED;
    ++root65_stats.callbacks;
    if (fd >= 0) ++root65_stats.fenced;
    else if (fd == -1) ++root65_stats.immediate;
    else ++root65_stats.invalid;
    pthread_mutex_unlock(&root65_lock);
    /* The queue is published BEFORE readiness; a stale eventfd wake is safe. */
    s65_notify_loop();
}
int sc_root_take_release(uint64_t *serial, int *release_fd) {
    if (!serial || !release_fd || root65_wake < 0) return 0;
    pthread_mutex_lock(&root65_lock);
    for (unsigned i = 0; i < S65_MAX_RELEASES; ++i) {
        struct s65_release_slot *s = &root65_slots[i];
        if (s->state != S65_CALLBACK_DELIVERED) continue;
        *serial = s->serial;
        *release_fd = s->fd;
        uint64_t trace_id=s->trace_id;
        s->state = S65_EMPTY;
        --root65_stats.outstanding;
        pthread_mutex_unlock(&root65_lock);
        if(s65_root_trace()) alroot_log("s65","S65_ANDROID_RELEASE serial=%llu ahb_id=%llu fd=%s",
            (unsigned long long)*serial,(unsigned long long)trace_id,
            *release_fd>=0 ? "fenced" : *release_fd==-1 ? "ready" : "invalid");
        return 1;
    }
    pthread_mutex_unlock(&root65_lock);
    return 0;
}
static struct s65_release_slot *s65_reserve(uint64_t serial) {
    struct s65_release_slot *slot = NULL;
    pthread_mutex_lock(&root65_lock);
    for (unsigned i = 0; i < S65_MAX_RELEASES; ++i) {
        if (root65_slots[i].state == S65_EMPTY && !slot) slot = &root65_slots[i];
        if (root65_slots[i].state != S65_EMPTY && root65_slots[i].serial == serial) {
            slot = NULL; break;
        }
    }
    if (slot) {
        *slot = (struct s65_release_slot){.state=S65_OWNED_BY_ANDROID,.serial=serial,.fd=-1};
        ++root65_stats.submits;
        if (++root65_stats.outstanding > root65_stats.high_water)
            root65_stats.high_water = root65_stats.outstanding;
    } else ++root65_stats.retries;
    pthread_mutex_unlock(&root65_lock);
    return slot;
}
void sc_root_get_stats(struct sc_root_stats *out) {
    if (!out) return;
    pthread_mutex_lock(&root65_lock);
    *out = root65_stats;
    pthread_mutex_unlock(&root65_lock);
}
int sc_root_active(void) { return root65.root.shown; }
uint64_t sc_root_serial(void) { return root65.root.shown ? root65.serial : 0; }

/* Mirror the owning SurfaceView's *advertised* fixed-source seamless vote
 * when the layer contains its actual frames. Never advertise an assumed
 * panel rate, increase the user's explicit cap, or request a display-mode
 * switch that is not seamless. Other legacy layers have a separate policy. */
static float s65_root_rate(void) {
    const int mhz=alroot_s65_effective_refresh_mhz();
    if(mhz<=0 || mhz>240000) return 0.f;
    const float ceiling=(float)mhz/1000.f, pref=atomic_load(&g_fps_want);
    return pref>0.f && pref<ceiling ? pref : ceiling;
}
static void s65_apply_vote(ASurfaceTransaction *tx,float hz) {
    if(root65.root.fps_applied==hz) return;
    /* A positive vote needs the exact strategy API (guarded before submit).
     * Never substitute another mode-change policy on an unusual platform. */
    if(api.setFrameRateStrategy) {
        /* Public Android NDK ABI enum values; resolved only when the strategy
         * entry point is present. Do not raise the entire APK's minSdk. */
        const int8_t fixed_source=1,only_if_seamless=0;
        api.setFrameRateStrategy(tx,root65.root.sc,hz,
            fixed_source,only_if_seamless);
        pthread_mutex_lock(&root65_lock);
        ++root65_stats.rate_vote_updates;
        root65_stats.rate_vote_mhz=(unsigned)(hz*1000.f+0.5f);
        pthread_mutex_unlock(&root65_lock);
    }
    root65.root.fps_applied=hz;
}

/* All allocation and capability checks precede setBufferWithRelease. Once it
 * owns the fd and slot there is no fallible operation before txApply. */
int sc_root_present_track_commit(AHardwareBuffer *ahb, int w, int h, int scene_w, int scene_h,
                                 int acquire_fd, uint64_t serial, int *armed) {
    int ret = 0, geo[8], ow, oh;
    AHardwareBuffer_Desc d;
    ASurfaceTransaction *tx = NULL;
    if(armed)*armed=0;
    root65_refusal="invalid_args";
    if (!ahb || !serial || acquire_fd < -1 || w < 1 || h < 1 ||
        scene_w != w || scene_h != h) goto done;
    if (!vkp_window()) {root65_refusal="no_window";goto done;}
    if (load_platform_api() || !api.setBufferWithRelease || !api.setBufferDataSpace) {
        root65_refusal="no_standard_api";goto done;
    }
    if (sc_root_event_fd()<0) {root65_refusal="eventfd";goto done;}
    AHardwareBuffer_describe(ahb, &d);
    root65_refusal="buffer";
    if (d.width != (uint32_t)w || d.height != (uint32_t)h || d.layers != 1 ||
        d.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
        !(d.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE)) goto done;
    root65_refusal="geometry";
    if (layer_geometry(w,h,scene_w,scene_h,geo) != 1) goto done;
    vkp_output_size(&ow,&oh);
    if (ow < 1 || oh < 1 || geo[0] < 0 || geo[1] < 0 || geo[2] > w || geo[3] > h ||
        geo[4] < 0 || geo[5] < 0 || geo[6] > ow || geo[7] > oh ||
        geo[0] >= geo[2] || geo[1] >= geo[3] || geo[4] >= geo[6] || geo[5] >= geo[7]) goto done;
    /* SurfaceControl has no portable nearest-neighbour sampler contract. */
    if (vkp_effects_blit_filter() == VK_FILTER_NEAREST &&
        (geo[2]-geo[0] != geo[6]-geo[4] || geo[3]-geo[1] != geo[7]-geo[5])) {
        root65_refusal="nearest_scaling";goto done;
    }
    const int bars = geo[4] || geo[5] || geo[6] != ow || geo[7] != oh;
    const int same = root65.root.shown && root65.serial == serial;
    if (same && root65.ahb != ahb) {root65_refusal="serial_alias";goto done;}
    const int geom = !root65.root.shown || memcmp(root65.rect,geo,sizeof(geo)) ||
                     ow != root65.ow || oh != root65.oh;
    const float fps = s65_root_rate();
    if(fps>0.f && !api.setFrameRateStrategy) {root65_refusal="frame_rate_api";goto done;}
    if (same && !geom && root65.root.fps_applied == fps) {root65_refusal="none";ret=2;goto done;}
    if (root65.root.win && root65.root.win != vkp_window()) {root65_refusal="window_changed";goto done;}
    if (!root65.root.sc) {
        root65.root.sc = api.createFromWindow(vkp_window(),"alroot_xwayland_direct_root");
        if (!root65.root.sc) {root65_refusal="no_parent";goto done;}
        root65.root.name="alroot_xwayland_direct_root";
        root65.root.win=vkp_window();
        root65.root.fps_applied=-1.f;
    }
    /* Black bars must hide any stale parent swapchain image. No animated
     * copy, host GPU clear, or CPU lock in steady state. */
    AHardwareBuffer *black = NULL;
    if (bars) {
        black=blank_buffer();
        if (!black) {root65_refusal="black_buffer";goto done;}
        if (!root65.back)
            root65.back=api.createFromWindow(vkp_window(),"alroot_xwayland_direct_bars");
        if (!root65.back) {root65_refusal="no_bars_layer";goto done;}
    }
    tx=api.txCreate();
    if (!tx) {root65_refusal="transaction";goto done;}
    struct s65_release_slot *slot = NULL;
    if (!same && !(slot=s65_reserve(serial))) {root65_refusal="release_pressure";ret=-1;goto done;}
    if (geom) {
        ARect from={geo[0],geo[1],geo[2],geo[3]}, to={geo[4],geo[5],geo[6],geo[7]};
        api.setGeometry(tx,root65.root.sc,&from,&to,0);
        if (bars) {
            ARect box={0,0,ow,oh}, black_src={0,0,16,16};
            if (!root65.bars) {
                api.setBuffer(tx,root65.back,black,-1);
                api.setBufferTransparency(tx,root65.back,ASC_TRANSPARENCY_OPAQUE);
                api.setZOrder(tx,root65.back,0);
                api.setVisibility(tx,root65.back,ASC_VISIBILITY_SHOW);
            }
            api.setGeometry(tx,root65.back,&black_src,&box,0);
        } else if (root65.bars) api.setVisibility(tx,root65.back,ASC_VISIBILITY_HIDE);
    }
    if (!root65.root.shown) {
        api.setZOrder(tx,root65.root.sc,1);
        api.setBufferTransparency(tx,root65.root.sc,ASC_TRANSPARENCY_OPAQUE);
        api.setBufferDataSpace(tx,root65.root.sc,ADATASPACE_SRGB);
        api.setVisibility(tx,root65.root.sc,ASC_VISIBILITY_SHOW);
    }
    s65_apply_vote(tx,fps);
#ifdef ALROOT_S66_ANDROID
    struct s66_time_record *obs=slot?s66_attach(tx,ahb,serial,acquire_fd):NULL;
#endif
    if (slot) {
        if(armed)*armed=s65_arm_commit(tx,serial);
        if(s65_root_trace()) {
            slot->trace_id=s65_trace_buffer_id(ahb);
            alroot_log("s65","S65_ANDROID_SET_BUFFER serial=%llu ahb_id=%llu acquire=%s src=%d,%d,%d,%d dst=%d,%d,%d,%d output=%dx%d",
                (unsigned long long)serial,(unsigned long long)slot->trace_id,acquire_fd>=0?"fd":"ready",
                geo[0],geo[1],geo[2],geo[3],geo[4],geo[5],geo[6],geo[7],ow,oh);
        }
        api.setBufferWithRelease(tx,root65.root.sc,ahb,acquire_fd,slot,s65_buffer_released);
        acquire_fd=-1; /* Android owns it even if this frame is dropped. */
    }
    api.txApply(tx);
#ifdef ALROOT_S66_ANDROID
    if(obs)obs->after=s65_commit_now();
#endif
    root65.root.shown=1; root65.serial=serial; root65.ahb=ahb;
    memcpy(root65.rect,geo,sizeof(geo));root65.ow=ow;root65.oh=oh;root65.bars=bars;
    vkp_signal_first_frame();
    root65_refusal="none";ret=same ? 2 : 1;
done:
    if (tx) api.txDelete(tx);
    if (acquire_fd >= 0) close(acquire_fd);
    return ret;
}
int sc_root_present(AHardwareBuffer *ahb,int w,int h,int scene_w,int scene_h,
                    int acquire_fd,uint64_t serial) {
    return sc_root_present_track_commit(ahb,w,h,scene_w,scene_h,acquire_fd,serial,NULL);
}

/* Removing a layer from the scene is not itself a consumer release. Outstanding
 * per-submit records survive this transaction and the destruction of the SC. */
static int s65_root_remove(int retire) {
    if (!root65.root.sc && !root65.back) return 1;
    if (!retire && !root65.root.shown) return 1;
    ASurfaceTransaction *tx=api.txCreate();
    if (!tx) return 0;
    if (root65.root.sc) {
        if (!retire) {
            AHardwareBuffer *blank=blank_buffer();
            if (!blank) { api.txDelete(tx);return 0; }
            api.setBuffer(tx,root65.root.sc,blank,-1);
        }
        if(root65.root.fps_applied>0.f) s65_apply_vote(tx,0.f);
        api.setVisibility(tx,root65.root.sc,ASC_VISIBILITY_HIDE);
        if (retire) api.reparent(tx,root65.root.sc,NULL);
    }
    if (root65.back) {
        api.setVisibility(tx,root65.back,ASC_VISIBILITY_HIDE);
        if (retire) api.reparent(tx,root65.back,NULL);
    }
    api.txApply(tx);api.txDelete(tx);
    pthread_mutex_lock(&root65_lock);
    ++root65_stats.hides;
    pthread_mutex_unlock(&root65_lock);
    root65.root.shown=0;root65.bars=0;root65.serial=0;root65.ahb=NULL;
    if (retire) {
        if (root65.back) api.release(root65.back);
        if (root65.root.sc) api.release(root65.root.sc);
        memset(&root65,0,sizeof(root65));
    }
    return 1;
}
int sc_root_hide(void) { return s65_root_remove(0); }
static void s65_root_window_gone(void) {
    if (++root65_window_epoch == 0) ++root65_window_epoch;
    if (s65_root_remove(1)) return;
    /* Last client-side refs are dropped if no transaction can be allocated.
     * Never announce completion; Android must still deliver each real callback. */
    if (root65.back) api.release(root65.back);
    if (root65.root.sc) api.release(root65.root.sc);
    memset(&root65,0,sizeof(root65));
}
void sc_root_end_epoch(void) {
    s65_root_window_gone();
    pthread_mutex_lock(&root65_lock);
    for (unsigned i=0;i<S65_MAX_RELEASES;++i) {
        if(root65_commits[i].serial) {
            root65_commits[i].serial=0;++root65_stats.commit_canceled;--root65_stats.commit_pending;
        }
        struct s65_release_slot *s=&root65_slots[i];
        if (s->state==S65_OWNED_BY_ANDROID) s->state=S65_ABANDONED;
        else if (s->state==S65_CALLBACK_DELIVERED) {
            if (s->fd>=0) close(s->fd);
            s->state=S65_EMPTY; --root65_stats.outstanding;
        }
    }
    pthread_mutex_unlock(&root65_lock);
}
