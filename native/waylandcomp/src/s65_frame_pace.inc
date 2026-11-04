/* Optional feedback timing only. This table does not pin a GPU image, send
 * wl_buffer.release or wp_presentation.presented, or change commit submission
 * rate. Each tracked callback uses the selected Android commit or S47 producer event.
 * A bounded watchdog prevents the Wayland client from hanging on a lost ack. */
#define S65_FRAME_SLOTS 16
#define S65_CALLBACK_WATCHDOG_NS 50000000LL
static struct {
    uint64_t token,source; /* source=immutable producer-use serial; 0=OnCommit */
    int64_t deadline,due_ns;
    unsigned char model; /* 4 = bounded monotonic callback tick, independent of Android OnCommit. */
    struct wl_list callbacks;
} s65_pace_slots[S65_FRAME_SLOTS];
/* The feature is OFF. Explicitly enabled trials default to producer readiness;
 * the two alternative timing models are available only in the private probe. */
#define S65_TRIAL_FRAME_MODEL 4
static int s65_commit_pace=S65_TRIAL_FRAME_MODEL,s65_timer_armed;
static uint64_t s65_source_reused,s65_source_peek;
static uint64_t s65_pending_frame_source,s65_source_ready,s65_source_deferred,s65_source_fire,s65_source_errors;
static uint64_t s65_new_frame_token,s65_commit_events,s65_commit_fire,s65_commit_timeout,s65_commit_no_slot;
static int64_t s65_next_frame_due_ns;
static uint64_t s65_timer4_offered,s65_timer4_fired,s65_timer4_failed;
static struct wl_event_source *s65_pace_timer;
static void s65_fire_slot(unsigned i,int cancel){
    if(!s65_pace_slots[i].token)return;
    uint64_t source=s65_pace_slots[i].source;
    if(source){s65_pace_slots[i].source=0;--s65_waiting_producer_frames;}
    else if(cancel && s65_pace_slots[i].model!=4)sc_root_cancel_commit(s65_pace_slots[i].token);
    if(!wl_list_empty(&s65_pace_slots[i].callbacks)){
        fire_frames(&s65_pace_slots[i].callbacks);
        g_s45_protocol_dirty=1;
        if(!cancel){if(s65_pace_slots[i].model==4)++s65_timer4_fired;else if(source)++s65_source_fire;else ++s65_commit_fire;}
    }
    if(cancel && s65_pace_slots[i].model==4)++s65_timer4_failed;
    s65_pace_slots[i].model=0;s65_pace_slots[i].due_ns=0;
    s65_pace_slots[i].token=0;
}
static void s65_rearm_callback_timer(void) {
    if(!s65_pace_timer)return;
    int64_t earliest=0;
    for(unsigned i=0;i<S65_FRAME_SLOTS;++i)if(s65_pace_slots[i].token){
        int64_t when=s65_pace_slots[i].deadline;
        if(s65_pace_slots[i].model==4 && s65_pace_slots[i].due_ns<when)when=s65_pace_slots[i].due_ns;
        if(!earliest || earliest>when)earliest=when;
    }
    if(!earliest){s65_timer_armed=0;return;}
    int64_t delta=earliest-now_ns();
    int ms=delta<=1000000 ? 1:(int)((delta+999999)/1000000);
    if(wl_event_source_timer_update(s65_pace_timer,ms)<0){
        s65_timer_armed=0;
        for(unsigned i=0;i<S65_FRAME_SLOTS;++i)if(s65_pace_slots[i].token){
            ++s65_commit_timeout;s65_fire_slot(i,1);
        }
        s45_flush_protocol();return;
    }
    s65_timer_armed=1;
}
static int s65_frame_watchdog(void *unused){
    s65_timer_armed=0;
    const int64_t t=now_ns();
    for(unsigned i=0;i<S65_FRAME_SLOTS;++i)if(s65_pace_slots[i].token){
        if(s65_pace_slots[i].model==4 && s65_pace_slots[i].due_ns<=t){s65_fire_slot(i,0);continue;}
        if(s65_pace_slots[i].deadline<=t){++s65_commit_timeout;s65_fire_slot(i,1);}
    }
    s65_rearm_callback_timer();s45_flush_protocol();return 0;
}
static int s65_frame_room(void){
    if(!s65_pace_timer)s65_pace_timer=wl_event_loop_add_timer(wl_display_get_event_loop(g_display),s65_frame_watchdog,NULL);
    if(s65_pace_timer)for(unsigned i=0;i<S65_FRAME_SLOTS;++i)if(!s65_pace_slots[i].token)return 1;
    ++s65_commit_no_slot;return 0;
}
static int s65_frame_can_track(void){
    if(!s65_commit_pace)return 0;
    struct surface *s;
    wl_list_for_each(s,&g_surfaces,link)if(!wl_list_empty(&s->frames))return s65_frame_room();
    return 0;
}
static void s65_note_commit_armed(uint64_t token,int track,int armed){
    s65_pending_frame_source=0;s65_new_frame_token=armed?token:0;
    if(track && !armed)++s65_commit_no_slot;
}
/* Watch the existing acquire FD only. A producer already latched complete
 * skips all scheduling work. A watch installed on attach is level-triggered:
 * a later signal wakes the WL loop directly, without the next output tick.
 * A cold use whose watch was installed just while assembling this draw gets
 * one terminal query so already-ready content still returns immediately. */
static void s65_note_direct_frame(uint64_t token,int track,int armed,struct s45_use *u) {
    s65_note_commit_armed(token,track,armed);
    if(s65_commit_pace==4){
        if(u->owner && !wl_list_empty(&u->owner->frames) && s65_frame_room()){
            s65_new_frame_token=token;s65_pending_frame_source=0;++s65_timer4_offered;
        }
        return;
    }
    if(s65_commit_pace!=2)return;
    if(u->producer_done>0){++s65_source_ready;return;}
    if(u->producer_done<0 || u->fd<0 || !u->producer_source || u->watch_fallback){++s65_source_errors;return;}
    /* The direct route validated this exact root/owner. Never hold a frame
     * just because an unrelated surface requested a callback. */
    if(!u->owner || wl_list_empty(&u->owner->frames))return;
    if(!u->s65_early_watch){
        ++s65_source_peek;
        const int status=s45_fence_status(u->fd);
        if(status){
            s47_producer_resolve(u,status);
            if(status>0)++s65_source_ready;else ++s65_source_errors;
            return;
        }
    }else ++s65_source_reused;
    if(!s65_frame_room())return;
    ++s65_source_deferred;s65_new_frame_token=token;s65_pending_frame_source=u->serial;
}
static void s65_frame_producer_terminal(uint64_t serial,int status){
    if(!serial || !status)return;
    for(unsigned i=0;i<S65_FRAME_SLOTS;++i)
        if(s65_pace_slots[i].token && s65_pace_slots[i].source==serial){
            if(status<0)++s65_source_errors;
            s65_fire_slot(i,status<0);
        }
}
static int s65_frame_snapshot(void){
    const uint64_t token=s65_new_frame_token;s65_new_frame_token=0;
    if(!token)return 0;
    for(unsigned i=0;i<S65_FRAME_SLOTS;++i)if(!s65_pace_slots[i].token){
        struct surface *s;struct wl_list *head=&s65_pace_slots[i].callbacks;wl_list_init(head);
        wl_list_for_each(s,&g_surfaces,link){
            wl_list_insert_list(head->prev,&s->frames);wl_list_init(&s->frames);
        }
        s65_pace_slots[i].source=s65_pending_frame_source;s65_pending_frame_source=0;
        if(s65_pace_slots[i].source)++s65_waiting_producer_frames;
        s65_pace_slots[i].token=token;s65_pace_slots[i].deadline=now_ns()+S65_CALLBACK_WATCHDOG_NS;
        s65_pace_slots[i].model=(s65_commit_pace==4)?4:0;
        if(s65_pace_slots[i].model==4){
            const int rate=alroot_s65_effective_refresh_mhz();
            /* Upstream Wayland frame callbacks are only requests to produce;
             * the Android acquire and release fences still prove ownership.
             * A reliable observed rate is mandatory; never infer it from 60Hz defaults. */
            if(rate<1000 || rate>240000){s65_fire_slot(i,1);return 0;}
            const int64_t period=1000000000000LL/rate,now=now_ns();
            if(!s65_next_frame_due_ns || s65_next_frame_due_ns<now-2*period ||
               s65_next_frame_due_ns>now+2*period)s65_next_frame_due_ns=now;
            s65_next_frame_due_ns+=period;
            s65_pace_slots[i].due_ns=s65_next_frame_due_ns;
            /* A user cap as low as 1 FPS is allowed. The 50ms default
             * watchdog must never fire BEFORE its valid capped deadline. */
            const int64_t safe_deadline=s65_next_frame_due_ns+S65_CALLBACK_WATCHDOG_NS;
            if(safe_deadline>s65_pace_slots[i].deadline)s65_pace_slots[i].deadline=safe_deadline;
        }else s65_pace_slots[i].due_ns=0;
        if(!s65_timer_armed || s65_pace_slots[i].model==4)s65_rearm_callback_timer();
        return 1;
    }
    ++s65_commit_no_slot;
    if(!s65_pending_frame_source && s65_commit_pace!=4)sc_root_cancel_commit(token);
    s65_pending_frame_source=0;return 0;
}
static void s65_drain_commit_events(void){
    uint64_t token;int64_t stamp;
    while(sc_root_take_commit(&token,&stamp)==1){
        ++s65_commit_events;
        for(unsigned i=0;i<S65_FRAME_SLOTS;++i)if(s65_pace_slots[i].token==token && !s65_pace_slots[i].source){
            s65_fire_slot(i,0);break;
        }
    }
}
static void s65_set_commit_pace(int mode){
    if(mode<0 || mode>4)mode=0;
    if(mode!=s65_commit_pace || !mode){
        for(unsigned i=0;i<S65_FRAME_SLOTS;++i)s65_fire_slot(i,1);
        if(s65_new_frame_token && !s65_pending_frame_source)sc_root_cancel_commit(s65_new_frame_token);
        s65_new_frame_token=s65_pending_frame_source=0;
        s45_flush_protocol();
    }
    if(mode!=4)s65_next_frame_due_ns=0;
    s65_commit_pace=mode;
}
static void s65_frame_end_epoch(void){
    s65_set_commit_pace(0);
    if(s65_pace_timer)wl_event_source_remove(s65_pace_timer);
    s65_pace_timer=NULL;s65_timer_armed=0;s65_new_frame_token=0;
    s65_prebooked=s65_due_now=0;s65_commit_events=s65_commit_fire=s65_commit_timeout=s65_commit_no_slot=0;
    s65_commit_pace=S65_TRIAL_FRAME_MODEL;s65_source_reused=s65_source_peek=0;
    s65_pace_fix=1;s65_source_ready=s65_source_deferred=s65_source_fire=s65_source_errors=0;
    s65_next_frame_due_ns=0;s65_timer4_offered=s65_timer4_fired=s65_timer4_failed=0;
}
static void s65_performance_snapshot(char *out,size_t n){
    struct timespec proc={0},thread={0};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&proc);clock_gettime(CLOCK_THREAD_CPUTIME_ID,&thread);
    struct sc_root_stats stats;sc_root_get_stats(&stats);
    snprintf(out,n," advertised_mhz=%d host_cpu_ns=%llu wl_cpu_ns=%llu pace_fix=%d prebook=%llu due_now=%llu"
                   " commit_pace=%d commit_events=%llu commit_fire=%llu commit_timeout=%llu commit_no_slot=%llu"
                   " producer_ready=%llu producer_deferred=%llu producer_fire=%llu producer_errors=%llu"
                   " rate_vote_mhz=%u rate_votes=%llu producer_peek=%llu producer_reused=%llu",
        atomic_load(&g_output_refresh_mhz),(unsigned long long)proc.tv_sec*1000000000ULL+proc.tv_nsec,
        (unsigned long long)thread.tv_sec*1000000000ULL+thread.tv_nsec,s65_pace_fix,
        (unsigned long long)s65_prebooked,(unsigned long long)s65_due_now,s65_commit_pace,
        (unsigned long long)s65_commit_events,(unsigned long long)s65_commit_fire,
        (unsigned long long)s65_commit_timeout,(unsigned long long)s65_commit_no_slot,
        (unsigned long long)s65_source_ready,(unsigned long long)s65_source_deferred,
        (unsigned long long)s65_source_fire,(unsigned long long)s65_source_errors,
        stats.rate_vote_mhz,(unsigned long long)stats.rate_vote_updates,(unsigned long long)s65_source_peek,(unsigned long long)s65_source_reused);
    size_t off=strnlen(out,n);
    if(off<n)snprintf(out+off,n-off," t4_offered=%llu t4_fired=%llu t4_failed=%llu",
       (unsigned long long)s65_timer4_offered,(unsigned long long)s65_timer4_fired,(unsigned long long)s65_timer4_failed);
}
