#define _GNU_SOURCE
#include "alroot_kgsl_ahb_fd.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

/* Android's VNDK native_handle_t structure (not part of the public NDK).
 * This opt-in KGSL bridge fails closed if the platform ABI is absent. */
struct alroot_native_handle {
    int version;
    int numFds;
    int numInts;
    int data[];
};

int alroot_kgsl_ahb_dup_image_fd(AHardwareBuffer *buffer,
                                 uint64_t *allocation_size,
                                 uint32_t *row_stride) {
    if (!buffer || !allocation_size || !row_stride) return -EINVAL;
    *allocation_size=0;
    *row_stride=0;
    void *native=dlopen("/system/lib64/libnativewindow.so",RTLD_NOW|RTLD_LOCAL);
    if (!native) return -ENOTSUP;
    const struct alroot_native_handle *(*get)(const AHardwareBuffer *)=
        dlsym(native,"AHardwareBuffer_getNativeHandle");
    void (*describe)(const AHardwareBuffer *,AHardwareBuffer_Desc *)=
        dlsym(native,"AHardwareBuffer_describe");
    if (!get||!describe) {dlclose(native);return -ENOTSUP;}
    AHardwareBuffer_Desc d={0};
    describe(buffer,&d);
    const struct alroot_native_handle *h=get(buffer);
    if (!h || h->version<12 || h->numFds<1 || h->numFds>8 ||
        h->numInts<0 || h->numInts>1024 || d.layers!=1 ||
        d.format!=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
        !d.width || d.height<1 || d.width>8192 || d.height>8192 ||
        d.stride<d.width || d.stride>16384) {
        dlclose(native);
        return -ENOTSUP;
    }
    /* Heuristic for the *image* FD, never the auxiliary gralloc metadata.
     * The pixel-exact cross-driver probe is a mandatory extra acceptance
     * check; an fd size match alone does not establish image layout. */
    uint64_t min_size=(uint64_t)d.stride*d.height*4;
    uint64_t tolerance=min_size/2;
    if (tolerance<65536) tolerance=65536;
    int selected=-1;
    uint64_t closest=UINT64_MAX,selected_size=0;
    int ambiguous=0;
    for (int i=0;i<h->numFds;i++) {
        struct stat st={0};
        if(h->data[i]<0 || fstat(h->data[i],&st)!=0 || st.st_size<=0)
            continue;
        uint64_t size=(uint64_t)st.st_size;
        if(size<min_size || size-min_size>tolerance) continue;
        if(size-min_size<closest) {
            selected=i;
            closest=size-min_size;
            selected_size=size;
            ambiguous=0;
        } else if(size-min_size==closest) ambiguous=1;
    }
    if (selected<0 || ambiguous) {dlclose(native);return -ENOTSUP;}
    int fd=fcntl(h->data[selected],F_DUPFD_CLOEXEC,3);
    dlclose(native);
    if(fd<0) return -errno;
    *allocation_size=selected_size;
    *row_stride=d.stride;
    return fd;
}
