/* S24: independent S26U Vulkan-wrapper BGRA AHB -> KGSL EGL DMA-BUF probe.
 * Only allocates short-lived Android buffers. No root, no X11 session writes.
 * Does not infer a LINEAR layout from FD size alone; the modifier test is
 * an explicit hypothesis, and visual output must still be checked separately.
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT 0x3443
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT 0x3444
#endif
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#endif
#define DRM_FOURCC_ARGB8888 0x34325241

struct native_handle_probe { int version, numFds, numInts, data[]; };

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: s24-wrapper-ahb-egl-import width height exact|no-overlay|gpu-only\n");
        return 2;
    }
    unsigned width = 0, height = 0;
    if (sscanf(argv[1], "%u", &width) != 1 ||
        sscanf(argv[2], "%u", &height) != 1 ||
        width < 64 || height < 64 || width > 2048 || height > 2048) return 2;
    uint64_t usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                     AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
                     AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY |
                     AHARDWAREBUFFER_USAGE_CPU_READ_RARELY |
                     AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    if (!strcmp(argv[3], "no-overlay"))
        usage &= ~AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
    else if (!strcmp(argv[3], "gpu-only"))
        usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
    else if (strcmp(argv[3], "exact")) return 2;

    void *native = dlopen("/system/lib64/libnativewindow.so", RTLD_NOW|RTLD_LOCAL);
    if (!native) { puts("S24_AHB_NATIVEWINDOW=UNAVAILABLE");return 2; }
    int (*allocate)(const AHardwareBuffer_Desc *,AHardwareBuffer **) =
        dlsym(native, "AHardwareBuffer_allocate");
    void (*release)(AHardwareBuffer *) = dlsym(native, "AHardwareBuffer_release");
    void (*describe)(const AHardwareBuffer *,AHardwareBuffer_Desc *) =
        dlsym(native, "AHardwareBuffer_describe");
    const struct native_handle_probe *(*handle_get)(const AHardwareBuffer *) =
        dlsym(native, "AHardwareBuffer_getNativeHandle");
    if (!allocate || !release || !describe || !handle_get) return 2;

    PFNEGLGETPLATFORMDISPLAYEXTPROC platform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    PFNEGLCREATEIMAGEKHRPROC image_create =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC image_destroy =
        (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (!platform || !image_create || !image_destroy) return 2;
    EGLDisplay egl = platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
    EGLint major=0,minor=0;
    if (egl==EGL_NO_DISPLAY || !eglInitialize(egl,&major,&minor)) {
        printf("S24_EGL_INITIALIZE=FAIL error:0x%x\n",eglGetError());return 3;
    }
    AHardwareBuffer_Desc desc = {0};
    desc.width=width;desc.height=height;desc.layers=1;desc.format=5;desc.usage=usage;
    AHardwareBuffer *buffer=NULL;
    int alloc_rc=allocate(&desc,&buffer);
    printf("S24_AHB_REQUEST=%ux%u usage:0x%" PRIx64 " format:5 mode:%s alloc_rc:%d\n",
           width,height,usage,argv[3],alloc_rc);
    if (alloc_rc || !buffer) {eglTerminate(egl);return 4;}
    describe(buffer,&desc);
    printf("S24_AHB_ACTUAL=%ux%u stride:%u format:%u usage:0x%" PRIx64 "\n",
           desc.width,desc.height,desc.stride,desc.format,desc.usage);
    const struct native_handle_probe *h=handle_get(buffer);
    uint64_t expected=(uint64_t)desc.stride*desc.height*4;
    uint64_t tolerance=expected/2 > 65536 ? expected/2 : 65536;
    int selected=-1;
    uint64_t nearest=UINT64_MAX;
    if (!h || h->numFds<1 || h->numFds>12) return 5;
    for (int i=0;i<h->numFds;i++) {
        struct stat st={0};
        if (fstat(h->data[i],&st)) continue;
        uint64_t size=st.st_size>0?(uint64_t)st.st_size:0;
        printf("S24_AHB_FD_%d=size:%" PRIu64 " expected:%" PRIu64 "\n",i,size,expected);
        if (size>=expected && size-expected<=tolerance) {
            if (size-expected<nearest) {nearest=size-expected;selected=i;}
            else if (size-expected==nearest) selected=-1;
        }
    }
    if (selected<0) {
        printf("S24_AHB_FD_SELECTION=NONE\n");release(buffer);eglTerminate(egl);return 6;
    }
    EGLint pitch=(EGLint)(desc.stride*4);
    const EGLint attrs[]={EGL_WIDTH,(EGLint)width,EGL_HEIGHT,(EGLint)height,
                          EGL_LINUX_DRM_FOURCC_EXT,DRM_FOURCC_ARGB8888,
                          EGL_DMA_BUF_PLANE0_FD_EXT,h->data[selected],
                          EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,
                          EGL_DMA_BUF_PLANE0_PITCH_EXT,pitch,
                          EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,0,
                          EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,0,EGL_NONE};
    EGLImageKHR image=image_create(egl,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,attrs);
    EGLint err=eglGetError();
    printf("S24_KGSL_EGL_DMA_BUF_IMPORT=%s error:0x%x pitch:%u bytes:%" PRIu64 "\n",
           image != EGL_NO_IMAGE_KHR ? "PASS" : "FAIL",err,desc.stride*4,expected);
    if (image!=EGL_NO_IMAGE_KHR)image_destroy(egl,image);
    release(buffer);
    eglTerminate(egl);
    dlclose(native);
    return image != EGL_NO_IMAGE_KHR ? 0:7;
}
