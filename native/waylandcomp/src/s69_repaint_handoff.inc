/* Bounded root direct -> parent transition. For a verified cooperating XWayland
 * only. Never infer fresh storage from a cookie, duplicate fd or use serial. */
#include "alroot-xwl-repaint-v1-server-protocol.h"
#include "s69_parent_recovery.inc"
static struct {
    struct wl_resource *manager,*root;
    struct wl_listener root_destroy;
    uint64_t old_id,old_serial,requests,fresh,latched,timeouts,refused;
    int64_t deadline,last_query;
    uint32_t cookie;
    int phase,unusable;
    const char *failure_reason;
    int64_t failed_ns;
    int64_t probe_until;
    int probe_handoff;
    uint64_t probe_withheld;
} s69h;
static void s69_handoff_reset(void){
    if(s69r.watching)s69_recovery_cancel("root_changed",now_ns());
    s69h.probe_handoff=0;s69h.probe_until=0;
    s69h.phase=0;s69h.old_id=s69h.old_serial=0;s69h.deadline=s69h.last_query=0;
    vkp_s69_cancel_present();
}
static void s69_forget_root(struct wl_listener *l,void *arg){
    (void)l;(void)arg;wl_list_remove(&s69h.root_destroy.link);
    s69h.root=NULL;s69_handoff_reset();
}
static void s69_repaint_gone(struct wl_resource *r){
    if(s69h.manager!=r)return;
    if(s69h.root){wl_list_remove(&s69h.root_destroy.link);s69h.root=NULL;}
    s69h.manager=NULL;s69_handoff_reset();
}
static void s69_register_root(struct wl_client *c,struct wl_resource *r,struct wl_resource *surface){
    const struct client_info *ci=client_info_of(c);unsigned long long ino;
    if(!ci || !ci->s65_root_peer || s69_live_peer(ci->pid,&ino)!=2 ||
       (s69h.manager && s69h.manager!=r) || s69h.root || !surface ||
       wl_resource_get_client(surface)!=c || !wl_resource_instance_of(surface,&wl_surface_interface,&surface_impl)){
        ++s69h.refused;return; /* an arbitrary or older client gets no capability */
    }
    s69h.manager=r;s69h.root=surface;
    s69h.root_destroy.notify=s69_forget_root;wl_resource_add_destroy_listener(surface,&s69h.root_destroy);
}
static void s69_destroy_repaint(struct wl_client *c,struct wl_resource *r){(void)c;wl_resource_destroy(r);}
static const struct alroot_xwl_repaint_v1_interface s69_repaint_impl={.destroy=s69_destroy_repaint,.register_root=s69_register_root};
static void s69_bind_repaint(struct wl_client *c,void *d,uint32_t v,uint32_t id){
    (void)d;(void)v;struct wl_resource *r=wl_resource_create(c,&alroot_xwl_repaint_v1_interface,1,id);
    if(!r){wl_client_post_no_memory(c);return;}
    wl_resource_set_implementation(r,&s69_repaint_impl,NULL,s69_repaint_gone);
}
static void s69_handoff_disable(const char *reason){
    s69h.failure_reason=reason;s69h.failed_ns=now_ns();
    s69r.epoch=sc_root_window_epoch();s69r.backoff=0;
    s69_recovery_cancel("await_baseline",s69h.failed_ns);
    ++s69h.timeouts;s69h.unusable=1;s69_handoff_reset();
    alroot_log("s69","fresh root/parent presentation not proven; disabled additional root-direct promotion for this peer");
}
static int s69_is_root(const struct s45_use *u){
    return s69h.manager && s69h.root && u && u->owner && u->owner->resource==s69h.root;
}
/* Only the actually displayed, registered surface may be requested. Its
 * core callback is merely permission to produce, never presentation/release. */
static int s69_wait_fresh(const struct draw_list *dl){
    if(!s69h.root || !s69h.manager || s69h.unusable || !s65_displayed || dl->n<1)return 0;
    struct surface *s=surface_for_image(dl->d[0].img);struct s45_use *u=s?s->gpu_use:NULL;
    if(!s69_is_root(u) || !u->buffer || !u->buffer->s63_ahb_id || u->buffer->s63_ahb_id!=s65_displayed_ahb)return 0;
    int64_t now=now_ns();
    if(!s69h.phase){
        s69h.probe_handoff=now<s69h.probe_until;s69h.probe_until=0;
        s69h.phase=1;s69h.old_id=s65_displayed_ahb;s69h.old_serial=s65_displayed_use;
        s69h.deadline=now+250000000LL;++s69h.requests;
        if(!++s69h.cookie)++s69h.cookie;
        fire_frames(&s->frames);
        alroot_xwl_repaint_v1_send_repaint(s69h.manager,s69h.root,s69h.cookie);
        wl_client_flush(wl_resource_get_client(s69h.manager));
    }
    if(now<s69h.deadline)return 1;
    s69_handoff_disable("fresh_root_timeout");return 0;
}
static void s69_arm_parent(const struct draw_list *dl){
    if(!s69h.phase)return;
    if(!s65_displayed || !s69h.root || dl->n<1){s69_handoff_reset();return;}
    struct surface *s=surface_for_image(dl->d[0].img);const struct s45_use *u=s?s->gpu_use:NULL;
    if(!s69_is_root(u)){s69_handoff_reset();return;}
    if(s69h.phase==1){
        if(!u->buffer || !u->external || !u->buffer->s63_ahb_id ||
           u->buffer->s63_ahb_id==s69h.old_id || u->serial==s69h.old_serial)return;
        s69h.phase=2;++s69h.fresh;s69h.deadline=now_ns()+300000000LL;
    }
    if(s69h.phase==2 && !vkp_s69_watch_present())s69_handoff_disable("parent_watch_unavailable");
}
static int s69_can_reveal_parent(void){
    if(s69h.phase!=2)return 1;
    const int64_t now=now_ns();
    /* Coalesce callbacks for the very same vsync, not a polling loop. */
    if(s69h.last_query && now-s69h.last_query<3000000LL)return 0;
    s69h.last_query=now;int proof=vkp_s69_parent_latched();
    if(s69h.probe_handoff && proof==1){++s69h.probe_withheld;proof=0;}
    if(proof==1){++s69h.latched;s69_handoff_reset();return 1;}
    if(proof<0 || now>=s69h.deadline){s69_handoff_disable(proof<0?"parent_timing_lost":"parent_timeout");return 1;}
    return 0;
}
static void s69_repaint_status(char *buf,size_t n){
    snprintf(buf,n,"registered=%d unavailable=%d phase=%d requests=%llu fresh=%llu latched=%llu failures=%llu rejected=%llu cookie=%u handoff_reason=%s handoff_failed_ns=%lld recovery_reason=%s recovery_epoch=%llu recovery_attempts=%llu recovery_proofs=%llu recovery_failures=%llu recovery_restored=%llu last_proof_ns=%lld probe_withheld=%llu",
        !!s69h.root,s69h.unusable,s69h.phase,(unsigned long long)s69h.requests,
        (unsigned long long)s69h.fresh,(unsigned long long)s69h.latched,(unsigned long long)s69h.timeouts,
        (unsigned long long)s69h.refused,s69h.cookie,s69h.failure_reason?s69h.failure_reason:"none",
        (long long)s69h.failed_ns,s69r.reason?s69r.reason:"none",(unsigned long long)s69r.epoch,
        (unsigned long long)s69r.attempts,(unsigned long long)s69r.proved,(unsigned long long)s69r.failures,
        (unsigned long long)s69r.restored,(long long)s69r.last_proof_ns,(unsigned long long)s69h.probe_withheld);
}
