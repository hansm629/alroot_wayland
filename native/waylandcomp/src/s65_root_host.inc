/* Host handoff of the single trusted opaque root to SurfaceControl.
 * All entries below live on the Wayland thread. Binder callbacks carry only
 * a process-monotonic token; never a surface, wl_resource or recycled use. */
#define S65_MAX_HOLDS 32
struct s65_hold {
    uint64_t token;
    struct s45_use *use;
    struct wl_event_source *watch;
    int fd;
};
static struct s65_hold s65_holds[S65_MAX_HOLDS];
static struct wl_event_source *s65_notifications;
static uint64_t s65_token, s65_displayed, s65_displayed_ahb, s65_displayed_use;
static unsigned s65_broken,s65_period,s65_hide_after_render;
static int64_t s65_same_backing_until;
/* A driver/allocation/window refusal should not add setup work to every
 * baseline frame; an eventfd/slot retry still wakes with no negative cache. */
static uint64_t s65_last_epoch,s65_base_epoch,s65_backoff_hits;
static int s65_base_seen,s65_last_request;
static int64_t s65_refused_until;
static const char *s65_last_reason="off";
static int s65_can_prime(uint64_t serial){
    return s65_control_enabled() && s65_commit_pace==2 && !s65_broken && serial==s65_displayed_use;
}
static void s65_root_option_changed(void){s65_refused_until=0;}
static void s65_parent_presented(void){
    s65_base_seen=1;s65_base_epoch=sc_root_window_epoch();
}
static struct s45_use *s65_reject(const char *why){s65_last_reason=why;return NULL;}
static struct {
    uint64_t eligible, submitted, same_serial, callbacks, android_fds, android_ready,
             signaled, hidden, retry, fallback, errors;
    unsigned pending_fds,max_held;
} s65_counts;

static unsigned s65_period_take(void) {unsigned v=s65_period; s65_period=0; return v;}

static void s65_root_snapshot(char *out,size_t n) {
    int written=snprintf(out,n,
        "eligible=%llu submitted=%llu same_serial=%llu callbacks=%llu android_fds=%llu android_ready=%llu "
        "signaled=%llu pending_fds=%u max_held=%u held=%u hidden=%llu retry=%llu fallback=%llu "
        "errors=%llu current_ahb=%llu current_use=%llu last_token=%llu",
        (unsigned long long)s65_counts.eligible,(unsigned long long)s65_counts.submitted,
        (unsigned long long)s65_counts.same_serial,(unsigned long long)s65_counts.callbacks,
        (unsigned long long)s65_counts.android_fds,(unsigned long long)s65_counts.android_ready,
        (unsigned long long)s65_counts.signaled,s65_counts.pending_fds,s65_counts.max_held,s65_held,
        (unsigned long long)s65_counts.hidden,(unsigned long long)s65_counts.retry,
        (unsigned long long)s65_counts.fallback,(unsigned long long)s65_counts.errors,
        (unsigned long long)s65_displayed_ahb,(unsigned long long)s65_displayed_use,
        (unsigned long long)s65_token);
    if(written>0 && (size_t)written<n)snprintf(out+written,n-(size_t)written,
        " reason=%s backoff=%llu epoch=%llu parent_ready=%d",s65_last_reason,
        (unsigned long long)s65_backoff_hits,(unsigned long long)sc_root_window_epoch(),
        s65_base_seen && s65_base_epoch==sc_root_window_epoch());
    size_t used=strnlen(out,n);if(used<n)s65_performance_snapshot(out+used,n-used);
}

static int s65_buffer_held(const struct dmabuf_buffer *b) {
    if(!s65_held || !b || !b->s63_ahb_id)return 0;
    for(unsigned i=0;i<S65_MAX_HOLDS;++i) {
        struct s45_use *u=s65_holds[i].use;
        if(u && u->buffer->s63_ahb_id==b->s63_ahb_id)return 1;
    }
    return 0;
}
static void s65_release_error(struct s65_hold *h,const char *why) {
    s65_broken=1;++s65_counts.errors;
    alroot_log("s65","Android release error: %s; unproven storage quarantined",why);
    if(h && h->use && h->use->resource) {
        wl_client_post_implementation_error(wl_resource_get_client(h->use->resource),
                                             "Android root release not proven: %s",why);
        g_s45_protocol_dirty=1;
    }
}
static void s65_close_watch(struct s65_hold *h) {
    if(h->watch){wl_event_source_remove(h->watch);h->watch=NULL;}
    if(h->fd>=0){close(h->fd);h->fd=-1;--s65_counts.pending_fds;}
}
static void s65_finish_hold(struct s65_hold *h) {
    s65_close_watch(h);
    --h->use->s65_refs;h->use=NULL;h->token=0;--s65_held;
}
static int s65_sync_status(int fd) {
    struct sync_file_info info={0};
    int r;do{r=ioctl(fd,SYNC_IOC_FILE_INFO,&info);}while(r<0 && errno==EINTR);
    return r<0 || info.status<0 ? -1 : info.status>0 ? 1 : 0;
}
static int s65_on_android_fence(int fd,uint32_t mask,void *data) {
    struct s65_hold *h=data;
    int status=s65_sync_status(fd);
    if(status==0 && !(mask&(WL_EVENT_ERROR|WL_EVENT_HANGUP)))return 0;
    if(status>0){++s65_counts.signaled;s65_finish_hold(h);}
    else {s65_close_watch(h);s65_release_error(h,"sync_file status");}
    s45_retire_ready();s45_flush_protocol();
    if(g_dirty)schedule_render();
    return 0;
}
static int s65_on_notifications(int fd,uint32_t mask,void *unused) {
    uint64_t token;int dep;
    sc_root_ack_events();
    while(sc_root_take_release(&token,&dep)==1) {
        struct s65_hold *h=NULL;
        for(unsigned i=0;i<S65_MAX_HOLDS;++i)
            if(s65_holds[i].use && s65_holds[i].token==token){h=&s65_holds[i];break;}
        if(!h){if(dep>=0)close(dep);continue;} /* prior disconnected host epoch */
        if(h->fd>=0){if(dep>=0)close(dep);s65_release_error(h,"duplicate completion");continue;}
        ++s65_counts.callbacks;
        if(dep==-1){++s65_counts.android_ready;s65_finish_hold(h);continue;}
        if(dep<0){s65_release_error(h,"invalid release payload");continue;}
        ++s65_counts.android_fds;h->fd=dep;++s65_counts.pending_fds;
        int status=s65_sync_status(dep);
        if(status>0){++s65_counts.signaled;s65_finish_hold(h);continue;}
        if(status==0)h->watch=wl_event_loop_add_fd(wl_display_get_event_loop(g_display),dep,
                                                    WL_EVENT_READABLE,s65_on_android_fence,h);
        if(status<0 || !h->watch){s65_close_watch(h);s65_release_error(h,"invalid FD or watch failed");}
    }
    s65_drain_commit_events();
    s45_retire_ready();s45_flush_protocol();
    if(g_dirty)schedule_render();
    return 0;
}

static struct s45_use *s65_candidate(const struct draw_list *dl,int w,int h) {
    if(dl->n!=1)return s65_reject("not-sole");
    if(w!=1920 || h!=1080)return s65_reject("root-size");
    if(alroot_color_hdr_open())return s65_reject("color-session");
    const struct vkp_draw *d=&dl->d[0];
    if(!d->external_ownership || !d->acquire_serial)return s65_reject("not-explicit");
    if(d->blend || !d->nested_output)return s65_reject("alpha-or-role");
    if(d->dx || d->dy || d->dw!=w || d->dh!=h || d->sx!=0 || d->sy!=0 || d->sw!=w || d->sh!=h)
        return s65_reject("viewport");
    struct surface *s=surface_for_image(d->img);
    if(!s || !s->drawn || !s->mapped || (s->role!=ROLE_TOPLEVEL && s->role!=ROLE_DESKTOP) ||
       s->buf_w!=w || s->buf_h!=h || s->s65_transform || (s->s65_scale && s->s65_scale!=1) ||
       !wl_list_empty(&s->children) || alroot_surface_color(s))return s65_reject("surface");
    const int *o=s->opaque;
    if(o[2]<=0 || o[3]<=0 || o[0]>0 || o[1]>0 || (int64_t)o[0]+o[2]<w || (int64_t)o[1]+o[3]<h)
        return s65_reject("opaque-region");
    struct client_info *ci=client_info_of(wl_resource_get_client(s->resource));
    struct s45_use *u=s->gpu_use;
    if(!ci || !ci->s65_root_peer)return s65_reject("peer");
    if(!u || u->owner!=s || u->retired || u->serial!=d->acquire_serial ||
       !u->external || !u->resource || u->buffer!=s->dmabuf_buf || u->s63_ticket ||
       !u->buffer->gpu_ahb || !u->buffer->s63_ahb_id || u->buffer->img!=d->img ||
       u->buffer->format!=DRM_ABGR8888 || u->producer_done<0 ||
       (!u->producer_done && u->fd<0))return s65_reject("use-or-format");
    return u;
}

#include "s69_repaint_handoff.inc"
static int s65_hide_displayed(void) {
    if(!s65_displayed)return 1;
    if(sc_root_active() && !sc_root_hide())return 0;
    s69_handoff_reset();s65_displayed=s65_displayed_ahb=s65_displayed_use=0;++s65_counts.hidden;return 1;
}
/* Return 1 = new Android submission, 2 = retained serial, 0 = baseline,
 * -1 = resource backpressure. Never hand the only producer FD to Android. */
static int s65_choose_route(const struct draw_list *dl,int w,int h,int supported_scene) {
    s65_hide_after_render=0;
    int requested=s65_control_enabled();
    if(s65_last_request!=requested){s65_refused_until=0;s65_last_request=requested;}
    if(s69r.watching && !requested)s69_recovery_cancel("disabled",now_ns());
    if(!requested && !s65_displayed && !s65_held){s65_last_reason="off";return 0;}
    uint64_t epoch=sc_root_window_epoch();
    if(s65_last_epoch!=epoch){s65_refused_until=0;s65_last_epoch=epoch;}
    if(!requested)s65_last_reason="off-drain";
    else if(s65_broken)s65_last_reason="quarantine";
    else if(!supported_scene)s65_last_reason="effect-or-hdr";
    struct s45_use *u=requested && !s65_broken && supported_scene ? s65_candidate(dl,w,h) : NULL;
    if(s69r.watching && (!s69_is_root(u) || s65_displayed || s65_held))
        s69_recovery_cancel("scene_changed",now_ns());
    if(u){
        s65_same_backing_until=0;
        ++s65_counts.eligible;
        vkp_apply_window_request();
        epoch=sc_root_window_epoch();
        if(s65_last_epoch!=epoch){s65_refused_until=0;s65_last_epoch=epoch;}
        if(!s65_base_seen || s65_base_epoch!=epoch){
            s65_last_reason="await-first-parent-buffer";goto fallback_scene;
        }
        if(!s65_displayed){
            const struct client_info *ci=client_info_of(wl_resource_get_client(u->owner->resource));
            if(s69h.unusable && s69_is_root(u) && !s65_held){
                if(s69_recover_parent(epoch,u->serial,u->frame_refs || vkp_image_busy(u->buffer->img),now_ns()))
                    s69h.unusable=0;
            }
            if(ci && ci->s65_root_peer==2 && (!s69_is_root(u) || s69h.unusable || !vkp_s69_can_watch_present())){
                s65_last_reason="refresh-or-parent-timing-unavailable";goto fallback_scene;
            }
        }else if(s69h.phase)s69_handoff_reset(); /* exposure/withdrawal reverted before new scene */
        if(s65_refused_until && now_ns()<s65_refused_until){
            ++s65_backoff_hits;goto fallback_scene;
        }
        if(!s65_notifications){
            int fd=sc_root_event_fd();
            if(fd>=0)s65_notifications=wl_event_loop_add_fd(wl_display_get_event_loop(g_display),fd,
                                                             WL_EVENT_READABLE,s65_on_notifications,NULL);
        }
        if(s65_notifications){
            int repeating=s65_displayed && sc_root_serial()==s65_displayed && s65_displayed_use==u->serial;
            struct s65_hold *slot=NULL;
            if(!repeating)for(unsigned i=0;i<S65_MAX_HOLDS;++i)
                if(!s65_holds[i].use){slot=&s65_holds[i];break;}
            if(!repeating && !slot){s65_last_reason="host-slots";++s65_counts.retry;return -1;}
            uint64_t token=repeating?s65_displayed:++s65_token;
            int needfd=!u->producer_done;
            int fd=needfd?fcntl(u->fd,F_DUPFD_CLOEXEC,0):-1;
            if(!needfd || fd>=0){
                int armed=0;
                int track=(!repeating && s65_commit_pace==1 && s65_frame_can_track());
                int ret=track ? sc_root_present_track_commit(u->buffer->gpu_ahb,w,h,w,h,fd,token,&armed)
                              : sc_root_present(u->buffer->gpu_ahb,w,h,w,h,fd,token);
                if(ret==1 && !repeating){
                    u->s65_presented=1;
                    s65_note_direct_frame(token,track,armed,u);
                    slot->use=u;slot->token=token;slot->fd=-1;slot->watch=NULL;
                    ++u->s65_refs;++s65_held;
                    if(s65_held>s65_counts.max_held)s65_counts.max_held=s65_held;
                    s65_displayed=token;s65_displayed_use=u->serial;s65_displayed_ahb=u->buffer->s63_ahb_id;
                    s65_last_reason="direct";++s65_counts.submitted;++s65_period;
                    if(s65_counts.submitted==1)alroot_log("s65","root direct ACTIVE: same AHB id=%llu use=%llu token=%llu",
                        (unsigned long long)s65_displayed_ahb,(unsigned long long)u->serial,(unsigned long long)token);
                    return 1;
                }
                if(ret==2 && repeating){s65_last_reason="retained";++s65_counts.same_serial;return 2;}
                if(ret<0){s65_last_reason="android-slots";++s65_counts.retry;return -1;}
                /* A refusal before apply leaves the producer dependency intact. */
                s65_last_reason=sc_root_last_refusal();
            }else s65_last_reason="fd-dup";
        }else s65_last_reason="event-registration";
        ++s65_counts.fallback;s65_refused_until=now_ns()+1000000000LL;
    }
fallback_scene:
    ;
    /* Different source: prepare/present the normal parent before detaching
     * the opaque child. If the parent stalls, keep the previous image.
     * Same source: Android may own FOREIGN layout. Give Xwayland one tick
     * to supply a fresh backing; without one, detach and await the real
     * release BEFORE reading, never assume reader/reader layout compatibility. */
    int busy=0,displayed_busy=0;
    if(s65_held)for(int i=0;i<dl->n;++i) {
        struct surface *s=surface_for_image(dl->d[i].img);
        if(s && s65_buffer_held(s->dmabuf_buf)){
            busy=1;
            if(s->dmabuf_buf->s63_ahb_id==s65_displayed_ahb)displayed_busy=1;
        }
    }
    if(busy){
        if(s65_displayed && displayed_busy){
            if(s69_wait_fresh(dl)){++s65_counts.retry;return -1;}
            int64_t now=now_ns();
            if(!s65_same_backing_until)s65_same_backing_until=now+8000000;
            if(now>=s65_same_backing_until)(void)s65_hide_displayed();
        }
        ++s65_counts.retry;return -1;
    }
    s65_same_backing_until=0;
    s69_arm_parent(dl);s65_hide_after_render=!!s65_displayed;
    return 0;
}
static void s65_stop_root(void) {
    (void)s65_hide_displayed();
    if(s65_notifications){wl_event_source_remove(s65_notifications);s65_notifications=NULL;}
    /* No client remains and no release event can be sent. Android/presenter
     * keep their own buffer references; future callbacks use fresh tokens. */
    for(unsigned i=0;i<S65_MAX_HOLDS;++i)if(s65_holds[i].use)s65_finish_hold(&s65_holds[i]);
    s65_frame_end_epoch();
    sc_root_end_epoch();
    s65_base_seen=s65_last_request=0;s65_refused_until=0;s65_backoff_hits=0;s65_last_reason="off";
    s65_broken=s65_period=s65_hide_after_render=0;s65_same_backing_until=0;
    memset(&s65_counts,0,sizeof(s65_counts));
}
