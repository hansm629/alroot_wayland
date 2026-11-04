/* Public API-33 frame timelines. No Java package refresh required.
 * Java's existing callback is a heartbeat; while the opt-in is active and the
 * NDK source is healthy it is NOT also an output tick. No timer/poll thread. */
#include <dlfcn.h>
#include <stdatomic.h>
#include <android/choreographer.h>
#include <stdint.h>
struct s66_time {int64_t frame,id,deadline,expected;};
struct s66_pub { _Atomic int64_t tag,id,deadline,expected; };
static struct s66_pub s66_pub[8];
static struct s66_time s66_tick;
static _Atomic unsigned s66_generation; /* odd = NDK owns the live tick */
static int s66_policy;
static _Thread_local struct {unsigned pending,index,gen;int64_t heartbeat,last_native,last_sent;AChoreographer *ch;} s66_ui;
static _Atomic uint64_t s66_native_count,s66_raw_count;
static struct {
 AChoreographer *(*get)(void);
 void (*post)(AChoreographer*,AChoreographer_vsyncCallback,void*);
 int64_t (*frame)(const AChoreographerFrameCallbackData*);
 size_t (*count)(const AChoreographerFrameCallbackData*);
 size_t (*pref)(const AChoreographerFrameCallbackData*);
 int64_t (*id)(const AChoreographerFrameCallbackData*,size_t);
 int64_t (*deadline)(const AChoreographerFrameCallbackData*,size_t);
 int64_t (*expected)(const AChoreographerFrameCallbackData*,size_t);
 int loaded;
} s66_ac;
static int s66_load_clock(void){
 if(s66_ac.loaded)return s66_ac.loaded>0;
 s66_ac.loaded=-1;void *l=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);if(!l)return 0;
 #define S66_SYM(field,name) do{*(void**)&s66_ac.field=dlsym(l,name);if(!s66_ac.field)return 0;}while(0)
 S66_SYM(get,"AChoreographer_getInstance");S66_SYM(post,"AChoreographer_postVsyncCallback");
 S66_SYM(frame,"AChoreographerFrameCallbackData_getFrameTimeNanos");
 S66_SYM(count,"AChoreographerFrameCallbackData_getFrameTimelinesLength");
 S66_SYM(pref,"AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex");
 S66_SYM(id,"AChoreographerFrameCallbackData_getFrameTimelineVsyncId");
 S66_SYM(deadline,"AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos");
 S66_SYM(expected,"AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos");
 #undef S66_SYM
 s66_ac.loaded=1;return 1;
}
static void s66_write_tick(int64_t frame,int slot,int generation);
static void s66_arm(unsigned gen);
static void s66_native(const AChoreographerFrameCallbackData *data,void *ptr){
 unsigned gen=(unsigned)(uintptr_t)ptr;if(s66_ui.pending==gen)s66_ui.pending=0;
 if(gen!=atomic_load(&s66_generation) || !data)return;
 int64_t now=now_ns();if(now-s66_ui.heartbeat>50000000LL)return; /* stopped Activity */
 int64_t frame=s66_ac.frame(data);size_t n=s66_ac.count(data),i=s66_ac.pref(data);
 if(frame>0 && frame>s66_ui.last_sent+1000000LL && n && i<n){
  int k=s66_ui.index++%8;struct s66_pub *p=&s66_pub[k];
  atomic_store(&p->tag,0);atomic_store(&p->id,s66_ac.id(data,i));
  atomic_store(&p->deadline,s66_ac.deadline(data,i));atomic_store(&p->expected,s66_ac.expected(data,i));
  atomic_store(&p->tag,frame);s66_ui.last_native=now;s66_ui.last_sent=frame;
  atomic_fetch_add(&s66_native_count,1);s66_write_tick(frame,k+1,(int)gen);
 }else if(frame>0 && i<n && s66_ui.last_sent && llabs(frame-s66_ui.last_sent)<=1000000LL){
  /* First native and final startup tick can be the same vsync. Prefer the
   * native callback on subsequent vsyncs, never emit the duplicate. */
  s66_ui.last_native=now;
 }
 s66_arm(gen);
}
static void s66_arm(unsigned gen){
 if(s66_ui.pending==gen || !s66_ui.ch)return;
 s66_ui.pending=gen;s66_ac.post(s66_ui.ch,s66_native,(void*)(uintptr_t)gen);
}
static int s66_java_tick(int64_t frame,unsigned gen){
 /* UI thread of the owning Activity/service only. Explicitly fails open to
  * its previous Java clock if APIs/Looper missing or delivery stalled. */
 if(!s66_load_clock())return 0;
 if(s66_ui.gen!=gen){s66_ui.gen=gen;s66_ui.last_sent=s66_ui.last_native=0;}
 if(!s66_ui.ch)s66_ui.ch=s66_ac.get();if(!s66_ui.ch)return 0;
 int64_t now=now_ns();s66_ui.heartbeat=now;s66_arm(gen);
 if(s66_ui.last_native && now-s66_ui.last_native<25000000LL)return 1;
 if(s66_ui.last_sent && frame<=s66_ui.last_sent+1000000LL)return 1;
 s66_ui.last_sent=frame;atomic_fetch_add(&s66_raw_count,1);return 0;
}
static void s66_clock_switch(int on){
 unsigned g=atomic_load(&s66_generation);
 if((g&1)!=(unsigned)(!!on))atomic_store(&s66_generation,g+1);
}
static void s66_take_tick(int64_t frame,int slot,int gen){
 s66_tick=(struct s66_time){0};if(!slot || (unsigned)(slot-1)>=8 || (unsigned)gen!=atomic_load(&s66_generation))return;
 struct s66_pub *p=&s66_pub[slot-1];if(atomic_load(&p->tag)!=frame)return;
 struct s66_time t={frame,atomic_load(&p->id),atomic_load(&p->deadline),atomic_load(&p->expected)};
 if(atomic_load(&p->tag)!=frame || t.id<0 || t.deadline<=frame || t.expected<t.deadline || t.expected-frame>250000000LL)return;
 s66_tick=t;
}
/* Read only by the same Wayland thread inside on_vsync. No guessed IDs. */
int alroot_s66_timeline(int64_t *id,int64_t *frame,int64_t *deadline,int64_t *target){
 if(!s66_tick.frame || !s66_policy)return 0;
 *id=s66_tick.id;*frame=s66_tick.frame;*deadline=s66_tick.deadline;*target=s66_tick.expected;
 return now_ns()<s66_tick.deadline;
}
