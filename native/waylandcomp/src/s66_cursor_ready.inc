/* Single Wayland-thread cursor selection. The idle wl_buffer owns the
 * resource lifetime; taking/destroying it cancels its watch before reuse.
 * Ready means safe to START the synchronous cursor bitmap readback, not
 * that an Android cursor bitmap can be handed out before readback completes. */
struct s66_cursor_state {
    struct surface *s;struct wl_resource *buffer;
    struct wl_event_source *source,*timer;
    int fd,plane,exported,no_watch,retry_ms;
    uintptr_t key;
};
static struct s66_cursor_state s66_cur={.fd=-1};
static uintptr_t s66_cursor_seq;
static void *s66_cursor_event_key(void){if(!++s66_cursor_seq)++s66_cursor_seq;s66_cur.key=s66_cursor_seq;return (void*)s66_cur.key;}
static void s66_cursor_progress(struct surface *s);
static void s66_cursor_close_fd(void){
    if(s66_cur.source){wl_event_source_remove(s66_cur.source);s66_cur.source=NULL;}
    if(s66_cur.fd>=0){close(s66_cur.fd);s66_cur.fd=-1;}
}
static void s66_cursor_cancel(struct surface *s){
    if(!s || s66_cur.s!=s)return;
    s66_cursor_close_fd();
    if(s66_cur.timer)wl_event_source_remove(s66_cur.timer);
    s66_cur=(struct s66_cursor_state){.fd=-1};
}
static int s66_cursor_timer(void *unused){
    if(!s66_cur.key || (uintptr_t)unused!=s66_cur.key)return 0;
    struct surface *s=s66_cur.s;
    if(s66_cur.timer){wl_event_source_remove(s66_cur.timer);s66_cur.timer=NULL;}
    if(s)s66_cursor_progress(s);return 0;
}
static int s66_cursor_backoff(void){
    /* Only a pending cursor with no usable fd watcher / retryable renderer.
     * Keep last valid cursor visible; no per-vsync polling. */
    if(!s66_cur.timer)s66_cur.timer=wl_event_loop_add_timer(wl_display_get_event_loop(g_display),s66_cursor_timer,s66_cursor_event_key());
    s66_cur.retry_ms=s66_cur.retry_ms==0?4:(s66_cur.retry_ms>=16?32:2*s66_cur.retry_ms);
    return s66_cur.timer&&wl_event_source_timer_update(s66_cur.timer,s66_cur.retry_ms)==0;
}
static int s66_cursor_fd_event(int fd,uint32_t mask,void *unused){
    if(!s66_cur.key || (uintptr_t)unused!=s66_cur.key)return 0;
    struct surface *s=s66_cur.s;
    if(!s||fd!=s66_cur.fd||!s66_cur.source)return 0;
    wl_event_source_remove(s66_cur.source);s66_cur.source=NULL;
    if(mask&(WL_EVENT_ERROR|WL_EVENT_HANGUP)){
        /* Do not turn an erroneous implicit reservation into read permission. */
        if(s==g_cursor_surface && s->idle_buffer==s66_cur.buffer){
            struct wl_resource *b=surface_take_idle(s);
            if(b)s45_release_idle(s,b);
        }else s66_cursor_cancel(s);
        return 0;
    }
    s66_cur.no_watch=1; /* If still pending despite readability, retry on bounded timer, not a level-trigger spin. */
    s66_cursor_progress(s);return 0;
}
static int s66_cursor_export(int fd,int *out,int *is_sync){
    struct s66_cursor_export_args {uint32_t flags;int32_t fd;} x={1u,-1}; /* DMA_BUF_SYNC_READ */
    int rv;do{rv=ioctl(fd,_IOWR('b',2,struct s66_cursor_export_args),&x);}while(rv<0&&errno==EINTR);
    if(!rv){if(x.fd<0)return -1;*out=x.fd;*is_sync=1;return 0;}
    if(errno!=ENOTTY&&errno!=EOPNOTSUPP&&errno!=EINVAL&&errno!=ENOSYS)return -1;
    /* Older kernels: dma-buf POLLIN is the implicit read-availability
     * signal. Watch a private reference; never wait with a positive timeout. */
    *out=fcntl(fd,F_DUPFD_CLOEXEC,0);*is_sync=0;return *out<0?-1:0;
}
static int s66_cursor_poll_implicit(int fd){
    struct pollfd p={.fd=fd,.events=POLLIN};
    int r;do{r=poll(&p,1,0);}while(r<0&&errno==EINTR);
    if(r<0||(p.revents&(POLLERR|POLLNVAL|POLLHUP)))return -1;
    return r>0&&(p.revents&POLLIN)?1:0;
}
static int s66_cursor_producer(struct surface *s,struct wl_resource *b,struct dmabuf_buffer *db,int *external){
    struct s45_use *u=s->gpu_use;*external=0;
    if(u&&u->external){
        if(u->owner!=s||u->resource!=b||u->buffer!=db||u->retired)return -1;
        *external=1;
        int status=s45_use_ready(u);
        if(status==0 && !u->producer_source && !s66_cursor_backoff())return -1;
        return status;
    }
    if(db->n_planes<1||db->n_planes>MAX_PLANES)return -1;
    /* A new attached wl_buffer resets plane/preparation state, including
     * another content generation of the very same wl_buffer resource. */
    while(s66_cur.plane<db->n_planes){
        int plane_fd=db->fd[s66_cur.plane];
        if(plane_fd<0)return -1;
        if(s66_cur.fd<0 && s66_cursor_export(plane_fd,&s66_cur.fd,&s66_cur.exported)<0)return -1;
        int ready=s66_cur.exported?vkp_release_fd_state(s66_cur.fd):s66_cursor_poll_implicit(s66_cur.fd);
        if(ready<0)return -1;
        if(!ready){
            if(!s66_cur.no_watch)s66_cur.source=wl_event_loop_add_fd(wl_display_get_event_loop(g_display),s66_cur.fd,WL_EVENT_READABLE,s66_cursor_fd_event,s66_cursor_event_key());
            return s66_cur.source||s66_cursor_backoff()?0:-1;
        }
        s66_cursor_close_fd();s66_cur.no_watch=s66_cur.retry_ms=0;++s66_cur.plane;
    }
    return 1;
}
static void s66_cursor_progress(struct surface *s){
    if(!s||s!=g_cursor_surface||s->role!=ROLE_CURSOR||!s->idle_buffer)return;
    struct wl_resource *b=s->idle_buffer;
    if(s66_cur.s!=s||s66_cur.buffer!=b){
        s66_cursor_cancel(s66_cur.s);s66_cur.s=s;s66_cur.buffer=b;
        /* The pending wl_resource has no callback identity until we register a source. */
    }
    if(s66_cur.source||s66_cur.timer)return;
    struct dmabuf_buffer *db=get_dmabuf(b);int ext=0,status=1;
    if(db&&vkp_frame_pending()){s45_arm();return;}
    if(db)status=s66_cursor_producer(s,b,db,&ext);
    if(status==0)return;
    if(status>0 && cursor_publish_buffer(s,b,ext)==0 && s66_cursor_backoff())return;
    /* Never read a failed dependency. Releasing an UNSAMPLED cursor is
     * allowed: committed explicit uses still retire via their S45 fences. */
    b=surface_take_idle(s);if(b)s45_release_idle(s,b);
}
