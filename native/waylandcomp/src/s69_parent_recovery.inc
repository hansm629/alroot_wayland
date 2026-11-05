/* Wayland-thread only, reached for a quarantined registered root. A tagged
 * baseline present proves the current parent can be displayed. It does not
 * release any buffer or fence. Promotion needs a fresh, idle source too. */
static struct {
    uint64_t epoch, serial, attempts, proved, failures, restored;
    int64_t retry_at, deadline, query_at, last_proof_ns;
    unsigned backoff;
    int watching, proven;
    const char *reason;
} s69r;

static void s69_recovery_cancel(const char *why, int64_t now) {
    if(s69r.watching)++s69r.failures;
    s69r.watching=s69r.proven=0;
    s69r.reason=why;
    /* No timer/render requests. Check again only on an ordinary new scene. */
    s69r.retry_at=now+(250000000LL << s69r.backoff);
    if(s69r.backoff<4)++s69r.backoff;
    vkp_s69_cancel_present();
}

static int s69_recover_parent(uint64_t epoch,uint64_t serial,int source_busy,int64_t now) {
    if(s69r.epoch!=epoch){
        s69_recovery_cancel("parent_epoch",now);
        s69r.epoch=epoch;s69r.retry_at=0;s69r.backoff=0;
    }
    if(!s69r.watching){
        if(now<s69r.retry_at)return 0;
        ++s69r.attempts;
        if(!vkp_s69_watch_present()){
            ++s69r.failures;s69_recovery_cancel("watch_unavailable",now);return 0;
        }
        s69r.watching=1;s69r.serial=serial;s69r.deadline=now+500000000LL;
        s69r.query_at=now+16000000LL;s69r.reason="await_parent";
        return 0;
    }
    if(serial!=s69r.serial)vkp_s69_disarm_present(); /* Do not tag a later source if the first draw was skipped. */
    if(now>=s69r.deadline){
        s69_recovery_cancel(s69r.proven?"no_fresh_idle_use":"parent_timeout",now);return 0;
    }
    if(!s69r.proven){
        if(now<s69r.query_at)return 0;
        s69r.query_at=now+16000000LL;
        int proof=vkp_s69_parent_latched();
        if(proof<0){s69_recovery_cancel("parent_timing_lost",now);return 0;}
        if(!proof)return 0;
        ++s69r.proved;s69r.proven=1;s69r.last_proof_ns=now;s69r.reason="await_idle_use";
    }
    if(source_busy || serial==s69r.serial)return 0;
    /* Recheck the watch's swapchain identity at admission too. Presentation
     * timing records were consumed above; a second query cannot replay them. */
    if(!vkp_s69_watch_current()){s69_recovery_cancel("swapchain_changed",now);return 0;}
    vkp_s69_cancel_present();s69r.watching=s69r.proven=0;s69r.backoff=0;
    ++s69r.restored;s69r.reason="restored";
    return 1;
}
