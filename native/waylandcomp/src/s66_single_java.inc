/* Used solely by the staged single-Java-clock APK variant. Both entries run
 * on the UI looper. Never calls AChoreographer_getInstance/postVsyncCallback. */
static _Atomic int s66_java_seen;
static _Atomic uint64_t s66_java_timed_ticks;
static inline int s66_java_capable(int supports) {
 if(!supports)return 0;
 if(!atomic_load_explicit(&s66_java_seen,memory_order_relaxed))atomic_store_explicit(&s66_java_seen,1,memory_order_relaxed);
 return 1;
}
int alroot_s66_single_raw(int64_t frame,int supports){
 int cap=s66_java_capable(supports);s66_write_tick(frame,0,0);
 return cap && (atomic_load(&s66_generation)&1);
}
int alroot_s66_single_data(int64_t frame,int64_t id,int64_t dl,int64_t target){
 unsigned gen=atomic_load(&s66_generation);if(!(gen&1)){s66_write_tick(frame,0,0);return 0;}
 if(frame<=0 || id<0 || dl<=frame || target<dl || target-frame>250000000LL){
  s66_write_tick(frame,0,0);return 1;
 }
 int k=s66_ui.index++%8;struct s66_pub *p=&s66_pub[k];
 atomic_store(&p->tag,0);atomic_store(&p->id,id);atomic_store(&p->deadline,dl);atomic_store(&p->expected,target);
 atomic_store(&p->tag,frame);atomic_fetch_add(&s66_java_timed_ticks,1);s66_write_tick(frame,k+1,(int)gen);return 1;
}
