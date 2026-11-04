/* Android transaction completion is for measurement only. It never retires
 * a wl_buffer, signals a frame callback, changes submission order, or waits.
 * Per-frame diagnostics only after the authenticated private Service opts in. */
#include <linux/sync_file.h>
#include <fcntl.h>
struct s66_time_record {
 uint64_t serial,ahb;int64_t frame,id,deadline,target,before,after,callback,latch,present;
 int32_t status,tagged,acquire,fd;
};
#define S66_ROWS 16384
#define S66_FDS 32
static struct s66_time_record *s66_rows;
static struct s66_time_record *s66_fds[S66_FDS];
static pthread_mutex_t s66_diag_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned s66_rows_n,s66_over,s66_late,s66_done;
static uint64_t s66_tagged,s66_untagged;
static int s66_trace_on,s66_dir=-1;
static int64_t s66_last_id=-1;
extern int alroot_s66_timeline(int64_t*,int64_t*,int64_t*,int64_t*);
/* Get the actual kernel fence timestamp only if all members have signaled.
 * A missing sync_file, unsupported present fence or empty timestamp stays
 * missing. Never use the arrival time of a Binder callback as presentation. */
static int s66_fence(int fd,int64_t *t){
 struct sync_file_info si={0};
 if(ioctl(fd,SYNC_IOC_FILE_INFO,&si)<0 || si.status<0)return -2;
 if(!si.status)return 0;
 if(!si.num_fences || si.num_fences>16)return -3;
 struct sync_fence_info fi[16]={0};si.sync_fence_info=(uint64_t)(uintptr_t)fi;
 if(ioctl(fd,SYNC_IOC_FILE_INFO,&si)<0 || si.status<=0 || si.num_fences>16)return -2;
 uint64_t max=0;
 for(unsigned j=0;j<si.num_fences;++j){
   if(fi[j].status<=0 || !fi[j].timestamp_ns || fi[j].timestamp_ns>INT64_MAX)return -3;
   if(fi[j].timestamp_ns>max)max=fi[j].timestamp_ns;
 }
 *t=(int64_t)max;return max?1:-3;
}
static void s66_poll_records(void){ /* lock held, trace only */
 for(unsigned i=0;i<S66_FDS;++i){
  struct s66_time_record *r=s66_fds[i];if(!r)continue;
  int s=s66_fence(r->fd,&r->present);
  if(s){r->status=s;close(r->fd);r->fd=-1;s66_fds[i]=NULL;}
 }
}
static void s66_completed(void *ctx,ASurfaceTransactionStats *st){
 struct s66_time_record *r=ctx;if(!r || !st)return;
 int64_t cb=s65_commit_now(),latch=api.latchTime(st);
 int fd=api.presentFd(st);
 pthread_mutex_lock(&s66_diag_lock);
 if(!s66_trace_on){++s66_late;pthread_mutex_unlock(&s66_diag_lock);if(fd>=0)close(fd);return;}
 r->callback=cb;r->latch=latch;++s66_done;
 if(fd<0)r->status=-1;
 else{
   r->status=s66_fence(fd,&r->present);
   if(r->status==0){
    for(unsigned i=0;i<S66_FDS;++i)if(!s66_fds[i]){s66_fds[i]=r;r->fd=fd;fd=-1;break;}
    if(fd>=0)r->status=-4;
   }
   if(fd>=0)close(fd);
 }
 pthread_mutex_unlock(&s66_diag_lock);
}
int sc_s66_diag(int en) {
 if(!en)return !s66_trace_on;
 if(s66_trace_on)return 1;
 if(s66_rows || load_platform_api() || !api.latchTime || !api.presentFd || !api.setOnComplete)return 0;
 const char *rt=getenv("XDG_RUNTIME_DIR");if(!rt)return 0;
 s66_dir=open(rt,O_DIRECTORY|O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(s66_dir<0)return 0;
 s66_rows=calloc(S66_ROWS,sizeof(*s66_rows));if(!s66_rows){close(s66_dir);s66_dir=-1;return 0;}
 pthread_mutex_lock(&s66_diag_lock);s66_trace_on=1;pthread_mutex_unlock(&s66_diag_lock);return 1;
}
static struct s66_time_record *s66_attach(ASurfaceTransaction *tx,AHardwareBuffer *a,uint64_t serial,int fd){
 int64_t id=-1,t=0,dl=0,p=0;int tag=0;
 if(alroot_s66_timeline(&id,&t,&dl,&p) && api.frameTimeline && id!=s66_last_id){
   api.frameTimeline(tx,id);s66_last_id=id;++s66_tagged;tag=1;
 }else ++s66_untagged;
 if(!s66_trace_on)return NULL;
 if(s66_rows_n==S66_ROWS){++s66_over;return NULL;}
 struct s66_time_record *r=&s66_rows[s66_rows_n++];
 *r=(struct s66_time_record){.serial=serial,.ahb=s65_trace_buffer_id(a),.frame=t,.id=id,.deadline=dl,.target=p,.tagged=tag,.acquire=fd>=0,.fd=-1,.status=-9};
 if(!(s66_rows_n%8)){pthread_mutex_lock(&s66_diag_lock);s66_poll_records();pthread_mutex_unlock(&s66_diag_lock);}
 api.setOnComplete(tx,r,s66_completed);r->before=s65_commit_now();return r;
}
void sc_s66_counts(uint64_t *timed,uint64_t *untimed,unsigned *completed){
 *timed=s66_tagged;*untimed=s66_untagged;*completed=0;
 if(s66_trace_on){pthread_mutex_lock(&s66_diag_lock);*completed=s66_done;pthread_mutex_unlock(&s66_diag_lock);}
}
void sc_s66_stop(void){
 s66_last_id=-1;
 if(!s66_trace_on)return;
 pthread_mutex_lock(&s66_diag_lock);s66_trace_on=0;s66_poll_records();
 for(unsigned i=0;i<S66_FDS;++i)if(s66_fds[i]){
  struct s66_time_record *r=s66_fds[i];close(r->fd);r->fd=-1;r->status=-5;s66_fds[i]=NULL;
 }
 unsigned done=s66_done;pthread_mutex_unlock(&s66_diag_lock); /* late callbacks cannot mutate rows */
 int fd=openat(s66_dir,"s66-transactions.tsv",O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY|O_CLOEXEC,0600);
 close(s66_dir);s66_dir=-1;if(fd<0)return;
 FILE *f=fdopen(fd,"w");if(!f){close(fd);return;}
 fprintf(f,"#S66_TX_V1 submitted=%u completed=%u overflow=%u; CLOCK_MONOTONIC; status 1=timestamp -1=unsupported_fd 0=pending -2=invalid -3=unavailable -4=slot_limit -5=shutdown -9=unobserved\n",s66_rows_n,done,s66_over);
 fputs("token\tahb\tframe\tvsync_id\tdeadline\ttarget\ttagged\tacquire_fd\tbefore_apply\tafter_apply\tcallback\tlatch\tpresent_signal\tstatus\n",f);
 for(unsigned i=0;i<s66_rows_n;++i){struct s66_time_record *q=&s66_rows[i];
 fprintf(f,"%llu\t%llu\t%lld\t%lld\t%lld\t%lld\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%lld\t%d\n",
 (unsigned long long)q->serial,(unsigned long long)q->ahb,(long long)q->frame,(long long)q->id,(long long)q->deadline,(long long)q->target,q->tagged,q->acquire,
 (long long)q->before,(long long)q->after,(long long)q->callback,(long long)q->latch,(long long)q->present,q->status);}
 fclose(f);
}
