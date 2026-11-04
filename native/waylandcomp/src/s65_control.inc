/* Bounded validation control. Only the compositor thread touches this state.
 * There is no per-frame getenv, file read, clock sample or socket IO. */
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <limits.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <stdlib.h>
static struct { int fd, enabled; char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct wl_event_source *input, *timer; dev_t dev; ino_t ino; unsigned int trials, expirations;
} s65_ctl={.fd=-1};
/* Same entry point used by the installed Activity's DisplayTiming. The separate
 * Service supplies the refresh actually observed on its own private display. */
extern void alroot_wayland_set_output_refresh_rate(int mhz);
extern int sc_s66_diag(int);
extern void sc_s66_stop(void);
extern void sc_s66_counts(uint64_t *,uint64_t *,unsigned *);
static int s65_private_probe;
static struct wl_event_source *s67_pool_lease_timer;
static int s67_pool_lease_expired(void *unused){(void)unused;vkp_s67_pool_trial(0);schedule_render();return 0;}
static int s67_pool_lease(const char *num){
    char *end=NULL;long secs=strtol(num,&end,10);
    if(num[0]<'1'||num[0]>'9'||*end||secs<1||secs>120)return 0;
    if(!s67_pool_lease_timer)s67_pool_lease_timer=wl_event_loop_add_timer(wl_display_get_event_loop(g_display),s67_pool_lease_expired,NULL);
    if(!s67_pool_lease_timer||wl_event_source_timer_update(s67_pool_lease_timer,(int)secs*1000)<0)return 0;
    vkp_s67_pool_trial(1);schedule_render();return 1;
}
static int s68_resume(void *unused){(void)unused;s68_baseline=0;schedule_render();return 0;}
static int s68_suspend(const char *s){
    char *end;long n=strtol(s,&end,10);if(s[0]<'1'||s[0]>'9'||*end||n<1||n>120)return 0;
    if(!s68_baseline_timer)s68_baseline_timer=wl_event_loop_add_timer(wl_display_get_event_loop(g_display),s68_resume,NULL);
    if(!s68_baseline_timer || wl_event_source_timer_update(s68_baseline_timer,(int)n*1000)<0)return 0;
    s68_baseline=1;schedule_render();return 1;
}
static int s65_control_enabled(void) {return s65_ctl.enabled;}
static void s65_ctl_switch(int on) {
    if (s65_ctl.enabled!=on) {s65_ctl.enabled=on;if(!on && !s65_private_probe)s66_policy=0; s66_clock_switch(on && s66_policy);s65_root_option_changed(); schedule_render();}
}
static int s65_ctl_expire(void *unused) {
    (void)unused; s65_ctl.expirations++; s65_ctl_switch(0);return 0;
}
static int s65_ctl_recv(int fd,uint32_t events,void *unused) {
    (void)unused;if(!(events&WL_EVENT_READABLE))return 0;
    for(int i=0;i<4;i++) {
        struct sockaddr_un peer={0}; char cmd[64],reply[1900],state[1536]={0};
        union { struct cmsghdr aligned; char bytes[CMSG_SPACE(sizeof(struct ucred))+CMSG_SPACE(4*sizeof(int))]; } anc;
        struct iovec io={.iov_base=cmd,.iov_len=sizeof(cmd)-1};
        struct msghdr msg={.msg_name=&peer,.msg_namelen=sizeof(peer),.msg_iov=&io,.msg_iovlen=1,
                           .msg_control=anc.bytes,.msg_controllen=sizeof(anc.bytes)};
        ssize_t n=recvmsg(fd,&msg,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
        if(n<0)return 0;
        int trusted=0,unexpected=0;
        for(struct cmsghdr *h=CMSG_FIRSTHDR(&msg);h;h=CMSG_NXTHDR(&msg,h)){
            if(h->cmsg_level==SOL_SOCKET && h->cmsg_type==SCM_CREDENTIALS && h->cmsg_len==CMSG_LEN(sizeof(struct ucred))){
                struct ucred c;memcpy(&c,CMSG_DATA(h),sizeof(c));trusted=(c.uid==getuid());
            }else {
                if(h->cmsg_level==SOL_SOCKET && h->cmsg_type==SCM_RIGHTS){
                    for(size_t j=0;j+sizeof(int)<=h->cmsg_len-CMSG_LEN(0);j+=sizeof(int)){
                        int sent;memcpy(&sent,(char*)CMSG_DATA(h)+j,sizeof(sent));close(sent);
                    }
                }
                unexpected=1;
            }
        }
        if(!trusted || unexpected || (msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC)) || n==0 || memchr(cmd,0,n) ||
           msg.msg_namelen<=offsetof(struct sockaddr_un,sun_path) || msg.msg_namelen>sizeof(peer))continue;
        cmd[n]=0;int valid=1;pid_t s69_peer=0;
        if(!strncmp(cmd,"native-baseline ",16))valid=s68_suspend(cmd+16);
        else if(!strcmp(cmd,"native-auto")){if(s68_baseline_timer)wl_event_source_timer_update(s68_baseline_timer,0);s68_resume(NULL);}
        else if(!strcmp(cmd,"native-status")){}
        else if(!strncmp(cmd,"xwl-peer-status ",16)){
            char *e=NULL;long v=strtol(cmd+16,&e,10);
            valid=(cmd[16]>='1'&&cmd[16]<='9'&&e&&!*e&&v>0&&v<=2147483647L);
            if(valid)s69_peer=(pid_t)v;
        }
        else if(!strncmp(cmd,"trial-pool ",11))valid=s67_pool_lease(cmd+11);
        else if(!strcmp(cmd,"pool-off")){if(s67_pool_lease_timer)wl_event_source_timer_update(s67_pool_lease_timer,0);vkp_s67_pool_trial(0);schedule_render();}
        else if(!strcmp(cmd,"pool-status")){}
        else if(!strcmp(cmd,"repaint-status")){}
        else if(!strcmp(cmd,"status")){}
        else if(s65_private_probe && !s65_ctl.enabled && !strncmp(cmd,"probe-refresh ",14)){
            char *end=NULL;long mhz=strtol(cmd+14,&end,10);
            if(*end || mhz<24000 || mhz>240000)valid=0;
            else {
                /* Reproduce DisplayTiming.surface.setFrameRate() on the probe's own
                 * SurfaceView; use the measured display rate, never system settings. */
                typedef int32_t (*s65_rate_fn)(ANativeWindow*,float,int8_t,int8_t);
                s65_rate_fn fn=(s65_rate_fn)dlsym(RTLD_DEFAULT,"ANativeWindow_setFrameRateWithChangeStrategy");
                /* NDK value for ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS.
                 * Its enum name is API 31, while this optional function is runtime-resolved. */
                const int8_t seamless_only = 0;
                ANativeWindow *w=vkp_window();
                if(!fn || !w || fn(w,(float)mhz/1000.f,ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                      seamless_only)!=0)valid=0;
                else alroot_wayland_set_output_refresh_rate((int)mhz);
            }
        }else if(s65_private_probe && !s65_ctl.enabled && !strncmp(cmd,"probe-model ",12) &&
            strlen(cmd)==15 && cmd[13]==' ' && (cmd[12]=='0'||cmd[12]=='1') && (cmd[14]>='0'&&cmd[14]<='4')){
            s65_set_pace_fix(cmd[12]=='1');s65_set_commit_pace(cmd[14]-'0');
        }
        else if(s65_private_probe && !s65_ctl.enabled && !strncmp(cmd,"probe-cap ",10)){
            char *end=NULL;long fps=strtol(cmd+10,&end,10);
            if(cmd[10]<'0' || cmd[10]>'9' || !end || *end || fps<0 || fps>240)valid=0;
            else g_fps_limit=(int)fps; /* Private test process only; managed settings are untouched. */
        }else if(s65_private_probe && !s65_ctl.enabled && strlen(cmd)==18 && !strncmp(cmd,"probe-timeline ",15) &&
                (cmd[15]=='0'||cmd[15]=='1') && cmd[16]==' ' && (cmd[17]=='0'||cmd[17]=='1')){
            if(!sc_s66_diag(cmd[17]-'0'))valid=0;else s66_policy=cmd[15]-'0';
        }else if(!strcmp(cmd,"off")){
            wl_event_source_timer_update(s65_ctl.timer,0);s65_ctl_switch(0);
        }else if(!strcmp(cmd,"on")){
            /* Authorized same-UID operator can re-enable only the strict
             * authenticated root-only SurfaceControl path. No GPU-wait bypass. */
            wl_event_source_timer_update(s65_ctl.timer,0);
            if(s66_policy){s66_policy=0;s66_clock_switch(0);}
            s65_ctl_switch(1);
        }else if(!strncmp(cmd,"trial-timeline ",15) && !s65_ctl.enabled && atomic_load(&s66_java_seen)){
            char *end=NULL;long sec=strtol(cmd+15,&end,10);
            if(cmd[15]<'1' || cmd[15]>'9' || *end || sec<1 || sec>120 || wl_event_source_timer_update(s65_ctl.timer,(int)sec*1000)<0)valid=0;
            else { s66_policy=1;s65_ctl.trials++;s65_ctl_switch(1); }
        }else if(!strncmp(cmd,"trial ",6)){
            char *end=NULL; long sec=strtol(cmd+6,&end,10);
            if(cmd[6]<'1'||cmd[6]>'9'||*end||sec<1||sec>120)valid=0;
            else if(wl_event_source_timer_update(s65_ctl.timer,(int)sec*1000)<0) valid=0;
            else {
                /* A normal user trial always has normal timing, even if it replaces a
                 * currently enabled timeline lease. Authenticated probe A/B retains its selector. */
                if(!s65_private_probe && s66_policy){ s66_policy=0;s66_clock_switch(0); }
                s65_ctl.trials++;s65_ctl_switch(1);
            }
        }else valid=0;
        s65_root_snapshot(state,sizeof(state));
        size_t joff=strlen(state);snprintf(state+joff,sizeof(state)-joff," java_owner=%d java_timed_ticks=%llu",atomic_load(&s66_java_seen),(unsigned long long)atomic_load(&s66_java_timed_ticks));
        size_t used=strlen(state);uint64_t tagged,untagged;unsigned completed;
        sc_s66_counts(&tagged,&untagged,&completed);
        snprintf(state+used,sizeof(state)-used," timeline=%d tx_timed=%llu tx_untimed=%llu tx_completed=%u ndk_ticks=%llu startup_fallback_ticks=%llu",s66_policy,(unsigned long long)tagged,(unsigned long long)untagged,completed,(unsigned long long)atomic_load(&s66_native_count),(unsigned long long)atomic_load(&s66_raw_count));
        if(!strncmp(cmd,"trial-pool ",11)||!strcmp(cmd,"pool-off")||!strcmp(cmd,"pool-status")){
            unsigned p[4];sc_s67_pool_stats(p);
            snprintf(state,sizeof(state),"pool_trial=%d pool_faulted=%d public_targets=%u fenced=%u ready=%u dropped=%u",vkp_s67_pool_policy(),vkp_s67_pool_faulted(),p[0],p[1],p[2],p[3]);
        }
        if(!strncmp(cmd,"native-",7))snprintf(state,sizeof(state),
            "native_auto=%d native_composed=%llu native_legacy=%llu native_explicit=%llu native_async=%llu native_fallback=%llu native_mailbox=%llu native_early=%llu native_capped=%llu mixed_composed=%llu mixed_fallback=%llu mixed_ineligible=%llu root_composed=%llu root_fallback=%llu root_ineligible=%llu",
            !s68_baseline,(unsigned long long)s68_passes,(unsigned long long)s68_legacy_passes,
            (unsigned long long)s68_explicit_passes,(unsigned long long)s68_async,(unsigned long long)s68_refused,(unsigned long long)s68_mailbox_tickets,(unsigned long long)s68_mailbox_early,(unsigned long long)s68_mailbox_capped,(unsigned long long)s69_mixed_passes,(unsigned long long)s69_mixed_fallback,(unsigned long long)s69_mixed_refused,
            (unsigned long long)s71_root_passes,(unsigned long long)s71_root_fallback,(unsigned long long)s71_root_refused);
        if(!strcmp(cmd,"repaint-status"))s69_repaint_status(state,sizeof(state));
        if(s69_peer){unsigned long long ino=0;int admitted=s69_live_peer(s69_peer,&ino);
            snprintf(state,sizeof(state),"queried_pid=%d scoped_rootful=%d refresh_peer=%d exec_ino=%llu",(int)s69_peer,!!admitted,admitted==2,ino);}
        int len=snprintf(reply,sizeof(reply),"S65CTL/1 ok=%d enabled=%d pid=%d trials=%u expirations=%u %s",valid,s65_ctl.enabled,(int)getpid(),s65_ctl.trials,s65_ctl.expirations,state);
        if(len>0 && (size_t)len<sizeof(reply))(void)sendto(fd,reply,(size_t)len,MSG_DONTWAIT|MSG_NOSIGNAL,
                (struct sockaddr*)&peer,msg.msg_namelen);
    }
    return 0;
}
static void s65_control_stop(void) {
    s69_handoff_reset();s69h.unusable=0;s69h.requests=s69h.fresh=s69h.latched=s69h.timeouts=s69h.refused=0;
    if(s68_baseline_timer){wl_event_source_remove(s68_baseline_timer);s68_baseline_timer=NULL;}
    s69_mixed_passes=s69_mixed_fallback=s69_mixed_refused=0;
    s71_root_passes=s71_root_fallback=s71_root_refused=0;
    s68_mailbox_tickets=s68_mailbox_early=s68_mailbox_capped=0;s68_fast_pending=0;
    s68_baseline=0;s68_passes=s68_legacy_passes=s68_explicit_passes=s68_refused=s68_async=0;
    vkp_s67_pool_trial(0);
    if(s67_pool_lease_timer){wl_event_source_remove(s67_pool_lease_timer);s67_pool_lease_timer=NULL;}
    s66_clock_switch(0);s66_policy=0;atomic_store(&s66_java_seen,0);sc_s66_stop();
    if(s65_ctl.input)wl_event_source_remove(s65_ctl.input);
    if(s65_ctl.timer)wl_event_source_remove(s65_ctl.timer);
    s65_ctl.input=s65_ctl.timer=NULL;s65_ctl.enabled=0;
    if(s65_ctl.fd>=0){close(s65_ctl.fd);s65_ctl.fd=-1;}
    struct stat st;
    if(s65_ctl.path[0] && lstat(s65_ctl.path,&st)==0 && S_ISSOCK(st.st_mode) &&
       st.st_dev==s65_ctl.dev && st.st_ino==s65_ctl.ino)unlink(s65_ctl.path);
    s65_ctl.path[0]=0;
}
#include "s70_control_bind.h"
static void s65_control_start(struct wl_display *d) {
    const char *rt=getenv("XDG_RUNTIME_DIR");struct sockaddr_un addr={.sun_family=AF_UNIX};
    struct stat st;
    const char *stage="validate private runtime";
    if(!rt||rt[0]!='/'){errno=EINVAL;goto preflight_fail;}
    if(lstat(rt,&st)<0)goto preflight_fail;
    if(!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&077)){errno=EACCES;goto preflight_fail;}
    int n=snprintf(addr.sun_path,sizeof(addr.sun_path),"%s/s65-root.ctl",rt);
    if(n<1 || (size_t)n>=sizeof(addr.sun_path)){errno=ENAMETOOLONG;goto preflight_fail;}
    stage="create control listener";
    int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)goto preflight_fail;
    s65_ctl.fd=fd;const int one=1;
    stage="enable credentials";
    if(setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&one,sizeof(one)))goto fail;
    const socklen_t alen=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+n+1);
    if(s70_bind_control_fd(fd,rt,&addr,alen,&stage)<0)goto fail;
    stage="record bound control socket";
    if(lstat(addr.sun_path,&st))goto fail;
    memcpy(s65_ctl.path,addr.sun_path,(size_t)n+1);s65_ctl.dev=st.st_dev;s65_ctl.ino=st.st_ino;
    stage="chmod control socket";
    if(chmod(addr.sun_path,0600))goto fail;
    struct wl_event_loop *loop=wl_display_get_event_loop(d);
    stage="register control event sources";
    s65_ctl.timer=wl_event_loop_add_timer(loop,s65_ctl_expire,NULL);
    s65_ctl.input=wl_event_loop_add_fd(loop,fd,WL_EVENT_READABLE,s65_ctl_recv,NULL);
    if(!s65_ctl.input||!s65_ctl.timer)goto fail;
    s65_private_probe=0;
    /* Private signed probe hosts run in two isolated Android processes.
     * Keep their diagnostics away from the managed Helper and make no
     * product policy change; the calling UID must also own this 0700 dir. */
    const int private_service=!strncmp(rt,"/data/data/com.termux/files/usr/tmp/s65-vd-",sizeof("/data/data/com.termux/files/usr/tmp/s65-vd-")-1);
    const int private_physical=!strncmp(rt,"/data/data/com.termux/files/usr/tmp/s65-vd-phys-",sizeof("/data/data/com.termux/files/usr/tmp/s65-vd-phys-")-1);
    if(private_service || private_physical){
        char procname[128]={0};int pf=open("/proc/self/cmdline",O_RDONLY|O_CLOEXEC);
        if(pf>=0){ssize_t nr=read(pf,procname,sizeof(procname)-1);close(pf);
            if(nr>0 && ((private_service && !strcmp(procname,"com.alroot.s65probe:private_display")) ||
                        (private_physical && !strcmp(procname,"com.alroot.s65probe:physical"))))s65_private_probe=1;}
    }
    const char *on=getenv("S65_XWL_ROOT_DIRECT");
    s65_ctl.trials=s65_ctl.expirations=0;
    /* Managed: enable only if an authenticated, single opaque Xwayland root
     * passes ALL S65 buffer/format/geometry/producer-fence guards.
     * Isolated diagnostic processes stay OFF until their explicit trial.
     * S65_XWL_ROOT_DIRECT=0 and the authenticated 'off' command are immediate
     * rollback paths, with Android release fences still retained. */
    s65_ctl.enabled=on ? !strcmp(on,"1") : !s65_private_probe;
    return;
fail:
    {int err=errno;alroot_log("s65","control startup failed at %s: %s (errno=%d)",stage,strerror(err),err);}
    s65_control_stop();return;
preflight_fail:
    {int err=errno;alroot_log("s65","control unavailable at %s: %s (errno=%d)",stage,strerror(err),err);}
}
