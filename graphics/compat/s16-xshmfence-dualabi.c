/* Experimental ALROOT S16 DRI3 Xshmfence two-libc ABI compatibility.
 * For private Bionic Xwayland ONLY, NOT glibc clients or global Termux.
 *
 * Bionic libxshmfence 1.3.3 pthread layout: memfd size 96B.
 * Debian glibc libxshmfence 1.3 futex layout: memfd size 4B.
 * An unmodified Bionic Xwayland reads the first word of the 4-byte futex
 * fence as a destroyed pthread_mutex and aborts in Bionic FORTIFY.
 * Interpose xshmfence_map_shm and fence operations; pass native 96B fences
 * through libxshmfence, route 4B fences to Linux shared-futex semantics.
 *
 * Never fake successful presentation: unknown sizes are rejected; failed
 * primitive lookups do not fall back to unsafe casts. Keep tracing optional.
 */
#define _GNU_SOURCE
#include <X11/xshmfence.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct glibc_fence_node {
    struct xshmfence *ptr;
    struct glibc_fence_node *next;
};
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct glibc_fence_node *g_glibc;
static _Atomic int g_printed_glibc, g_printed_bionic;

static void *original(const char *symbol) {
    void *fn = dlsym(RTLD_NEXT, symbol);
    if (!fn) {
        fprintf(stderr, "ALROOT_S16_XSHMFENCE_FATAL=missing-original-%s\n",symbol);
        abort();
    }
    return fn;
}
static bool glibc_pointer(const struct xshmfence *ptr) {
    bool yes = false;
    pthread_mutex_lock(&g_lock);
    for (struct glibc_fence_node *n=g_glibc;n;n=n->next)
        if (n->ptr == ptr) {yes=true;break;}
    pthread_mutex_unlock(&g_lock);
    return yes;
}
static void add_glibc(struct xshmfence *ptr) {
    struct glibc_fence_node *n = malloc(sizeof(*n));
    if (!n) abort();
    n->ptr=ptr;
    pthread_mutex_lock(&g_lock);
    n->next=g_glibc;g_glibc=n;
    pthread_mutex_unlock(&g_lock);
}
static int del_glibc(struct xshmfence *ptr) {
    pthread_mutex_lock(&g_lock);
    struct glibc_fence_node **loc=&g_glibc;
    while (*loc && (*loc)->ptr!=ptr)loc=&(*loc)->next;
    int yes=*loc!=NULL;
    if (yes){struct glibc_fence_node *dead=*loc;*loc=dead->next;free(dead);}
    pthread_mutex_unlock(&g_lock);
    return yes;
}
static int *word(struct xshmfence *ptr){return (int *)(void *)ptr;}
static const int *constword(const struct xshmfence *ptr){return (const int *)(const void *)ptr;}
static void info_once(const char *which, _Atomic int *seen) {
    if (!getenv("S16_XSHMFENCE_TRACE")) return;
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(seen, &expected, 1,
                                                memory_order_relaxed,
                                                memory_order_relaxed))
        fprintf(stderr,"ALROOT_S16_XSHMFENCE_MAPPED_FORMAT=%s\n",which);
}
struct xshmfence *xshmfence_map_shm(int fd) {
    struct stat st={0};
    if (fd<0 || fstat(fd,&st)!=0)return NULL;
    if (st.st_size==4) {
        void *m=mmap(NULL,4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if (m==MAP_FAILED)return NULL;
        add_glibc(m);
        info_once("GLIBC_FUTEX_4B",&g_printed_glibc);
        return m;
    }
    if (st.st_size==96){
        info_once("BIONIC_PTHREAD_96B",&g_printed_bionic);
        struct xshmfence *(*real)(int)=original("xshmfence_map_shm");
        return real(fd);
    }
    fprintf(stderr,"ALROOT_S16_XSHMFENCE_UNSUPPORTED_SIZE=%lld\n",(long long)st.st_size);
    errno=EINVAL;
    return NULL;
}
void xshmfence_unmap_shm(struct xshmfence *f) {
    if(del_glibc(f)){munmap(f,4);return;}
    void (*real)(struct xshmfence*)=original("xshmfence_unmap_shm");
    real(f);
}
int xshmfence_alloc_shm(void) {
    int (*real)(void)=original("xshmfence_alloc_shm");
    return real(); /* Bionic's 96B native producer ABI stays unchanged */
}
int xshmfence_trigger(struct xshmfence *f) {
    if(!glibc_pointer(f)){
        int (*real)(struct xshmfence*)=original("xshmfence_trigger");
        return real(f);
    }
    int expected=0;
    if (atomic_compare_exchange_strong_explicit((_Atomic int*)word(f),&expected,1,
                                                memory_order_seq_cst,memory_order_seq_cst))
        return 0;
    if (expected == -1) {
        atomic_store_explicit((_Atomic int*)word(f),1,memory_order_seq_cst);
        return syscall(SYS_futex,word(f),FUTEX_WAKE,INT_MAX,NULL,NULL,0)<0?-1:0;
    }
    return 0;
}
int xshmfence_await(struct xshmfence *f) {
    if (!glibc_pointer(f)){
        int (*real)(struct xshmfence*)=original("xshmfence_await");
        return real(f);
    }
    for (;;) {
        int expected=0;
        if (!atomic_compare_exchange_strong_explicit((_Atomic int*)word(f),&expected,-1,
                                                     memory_order_seq_cst,memory_order_seq_cst)) {
            if(expected==1)return 0;
        }
        if(syscall(SYS_futex,word(f),FUTEX_WAIT,-1,NULL,NULL,0)<0 && errno!=EAGAIN && errno!=EINTR)
            return -1;
    }
}
int xshmfence_query(struct xshmfence *f) {
    if (!glibc_pointer(f)){
        int (*real)(struct xshmfence*)=original("xshmfence_query");
        return real(f);
    }
    return atomic_load_explicit((const _Atomic int*)constword(f),memory_order_seq_cst)==1;
}
void xshmfence_reset(struct xshmfence *f) {
    if (!glibc_pointer(f)) {
        void (*real)(struct xshmfence*)=original("xshmfence_reset");
        real(f);return;
    }
    int expected=1;
    atomic_compare_exchange_strong_explicit((_Atomic int*)word(f),&expected,0,
                                            memory_order_seq_cst,memory_order_seq_cst);
}
