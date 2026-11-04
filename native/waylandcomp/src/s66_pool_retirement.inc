/* Legacy SurfaceControl pools, owner-thread writes; Android releases on Binder threads.
 * Stable, bounded slots are NEVER reset while the old consumer could still use
 * them. Valid callbacks are tied to both a submission generation and an SC
 * ref retained until its last stats query (also on Android 10/11). */
#define S66_SC_OWNERS 32
#define S66_COMPLETIONS 64
struct s66_sc_owner {ASurfaceControl *sc; unsigned callbacks,retired;};
struct complete_ctx { struct s66_sc_owner *owner; struct slot *slot; uint64_t use; void *token; };
static struct s66_sc_owner s66_owners[S66_SC_OWNERS];
static struct complete_ctx s66_ctx[S66_COMPLETIONS];
static uint64_t s66_generation;
static _Atomic int s66_token_quarantine;
static unsigned s66_retired; /* modified only by the Wayland thread */
static struct s66_sc_owner *s66_find_owner(ASurfaceControl *sc){
    for(unsigned i=0;i<S66_SC_OWNERS;i++)if(s66_owners[i].sc==sc)return &s66_owners[i];
    return NULL;
}
static ASurfaceControl *s66_new_sc(ANativeWindow *w,const char *name){
    /* Only the Wayland thread creates/retires owners; callbacks only make
     * more space. Never allocate an untracked SC when the fixed table is full. */
    pthread_mutex_lock(&g_lock);struct s66_sc_owner *o=s66_find_owner(NULL);pthread_mutex_unlock(&g_lock);
    if(!o)return NULL;
    ASurfaceControl *sc=api.createFromWindow(w,name);
    if(sc){pthread_mutex_lock(&g_lock);*o=(struct s66_sc_owner){.sc=sc};pthread_mutex_unlock(&g_lock);}
    return sc;
}
static ASurfaceControl *s66_take_owner_if_done(struct s66_sc_owner *o){
    if(!o||!o->retired||o->callbacks)return NULL;
    ASurfaceControl *sc=o->sc;memset(o,0,sizeof(*o));return sc;
}
static void s66_drop_owner(ASurfaceControl *sc){
    if(!sc)return;
    pthread_mutex_lock(&g_lock);struct s66_sc_owner *o=s66_find_owner(sc);
    if(o)o->retired=1;
    ASurfaceControl *ready=s66_take_owner_if_done(o);pthread_mutex_unlock(&g_lock);
    if(ready)api.release(ready);
}
static void on_complete(void *context,ASurfaceTransactionStats *stats){
    struct complete_ctx *ctx=context;
    /* This ctx keeps .sc alive through the call even when the retiring
     * transaction's callback races earlier transaction callbacks. Missing
     * stats or an invalid payload are NOT Android's already-released -1. */
    const int fd=stats?api.prevReleaseFence(stats,ctx->owner->sc):-2;
    const int valid=fd==-1||(fd>=0 && vkp_release_fd_state(fd)>=0);
    void *token=ctx->token;int owned_fd=fd;
    pthread_mutex_lock(&g_lock);
    struct slot *s=ctx->slot;
    if(s && s->busy && s->submission==ctx->use && valid){
        if(s->release_fd>=0)close(s->release_fd);
        s->release_fd=owned_fd;owned_fd=-1;s->busy=0;
    } /* Invalid proof: keep the original slot busy/quarantined. */
    struct s66_sc_owner *o=ctx->owner;--o->callbacks;
    memset(ctx,0,sizeof(*ctx));
    ASurfaceControl *drop=s66_take_owner_if_done(o);
    pthread_mutex_unlock(&g_lock);
    if(token && valid){ahb_swapchain_layer_released(token,owned_fd);owned_fd=-1;}
    else if(token)atomic_store(&s66_token_quarantine,1); /* stop accepting new untracked external storage */
    if(owned_fd>=0)close(owned_fd);
    if(drop)api.release(drop);
}
static int add_previous(ASurfaceTransaction *tx,struct layer *l){
    if(l->cur_slot<0&&!l->cur_token)return 0;
    if(!l->sc || l->cur_slot>=POOL_MAX||(l->cur_token&&l->cur_slot>=0))return -1;
    pthread_mutex_lock(&g_lock);
    struct s66_sc_owner *o=s66_find_owner(l->sc);struct complete_ctx *c=NULL;
    if(o && !o->retired)for(unsigned i=0;i<S66_COMPLETIONS;++i)if(!s66_ctx[i].owner){c=&s66_ctx[i];break;}
    if(c){
        struct slot *v=l->cur_slot>=0?&l->slots[l->cur_slot]:NULL;
        *c=(struct complete_ctx){.owner=o,.slot=v,.use=v?v->submission:0,.token=l->cur_token};
        ++o->callbacks;
    }
    pthread_mutex_unlock(&g_lock);
    if(!c)return -1;
    api.setOnComplete(tx,c,on_complete);return 0;
}
/* Poll signaled status only, never sleep. Transfer objects privately under
 * the mutex; all Vulkan/AHB destructors run on the Wayland thread outside
 * the mutex. The helper VkDevice lives for the process. On session stop
 * held resources remain bounded/quarantined across restart, not force-freed. */
static void s66_reap_slots(void){
    if(!s66_retired)return;
    struct slot gone[SC_LAYER_COUNT*POOL_MAX];unsigned n=0;
    pthread_mutex_lock(&g_lock);
    for(int i=0;i<SC_LAYER_COUNT;i++)for(int j=0;j<g_layers[i].pool_n;j++){
        struct slot *s=&g_layers[i].slots[j];
        if(!s->retired||s->busy)continue;
        if(s->release_fd>=0){
            int state=vkp_release_fd_state(s->release_fd);
            if(state==0)continue;
            if(state<0){s->busy=1;continue;} /* quarantine (never treat POLLERR as success) */
        }
        gone[n++]=*s;*s=(struct slot){.release_fd=-1};--s66_retired;
    }
    pthread_mutex_unlock(&g_lock);
    for(unsigned i=0;i<n;i++){
        if(gone[i].release_fd>=0)close(gone[i].release_fd);
        if(gone[i].img)vkp_image_destroy(gone[i].img);
        if(gone[i].ahb)AHardwareBuffer_release(gone[i].ahb);
    }
}
static void s66_detach_slots(void){
    pthread_mutex_lock(&g_lock);
    for(int i=0;i<SC_LAYER_COUNT;i++){
        struct layer *l=&g_layers[i];
        for(int j=0;j<l->pool_n;j++){
            struct slot *s=&l->slots[j];if(s->ahb&&!s->retired){s->retired=1;++s66_retired;}
        }
        l->first_logged=0;l->logged_w=l->logged_h=0;
    }
    pthread_mutex_unlock(&g_lock);s66_reap_slots();
}
