/* GPU-S1.5 discovery: proof of native Turnip GPU writes to a real Android
 * gralloc AHB using VK_EXT_external_memory_dma_buf.  This test does NOT
 * modify Turnip's WSI, change APKs, install libraries or perform CPU pixel
 * upload.  The five diagnostic pixels are read only AFTER GPU completion,
 * through Android's existing vendor EGL, to ensure the Vulkan driver wrote
 * to the identical AHardwareBuffer that the compositor will import.
 */
#define VK_USE_PLATFORM_ANDROID_KHR 1
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "alroot_kgsl_ahb_fd.h"

/* Only for the opt-in BGRA8 diagnostic. The ordinary KGSL helper remains
 * unchanged and intentionally rejects non-RGBA8 buffers. */
struct s15_native_handle {
    int version;
    int numFds;
    int numInts;
    int data[];
};

static int duplicate_bgra_image_fd(void *lib,
                                   const AHardwareBuffer *buffer,
                                   uint64_t *bytes, uint32_t *stride) {
    const struct s15_native_handle *(*get)(const AHardwareBuffer *)=
        dlsym(lib,"AHardwareBuffer_getNativeHandle");
    void (*describe)(const AHardwareBuffer *,AHardwareBuffer_Desc *)=
        dlsym(lib,"AHardwareBuffer_describe");
    if(!get||!describe)return -1;
    AHardwareBuffer_Desc d={0};describe(buffer,&d);
    if(d.format!=5||!d.width||!d.height||d.stride<d.width)return -1;
    const struct s15_native_handle *h=get(buffer);
    if(!h||h->version<12||h->numFds<1||h->numFds>8)return -1;
    uint64_t size=(uint64_t)d.stride*d.height*4;
    uint64_t limit=size/2>65536?size/2:65536;
    int index=-1;uint64_t nearest=UINT64_MAX;
    for(int i=0;i<h->numFds;i++) {
        struct stat st={0};
        if(h->data[i]<0||fstat(h->data[i],&st)!=0||st.st_size<=0)continue;
        uint64_t candidate=(uint64_t)st.st_size;
        if(candidate<size||candidate-size>limit)continue;
        if(candidate-size<nearest){nearest=candidate-size;index=i;*bytes=candidate;}
        else if(candidate-size==nearest)return -1;
    }
    if(index<0)return -1;
    *stride=d.stride;
    return fcntl(h->data[index],F_DUPFD_CLOEXEC,3);
}

static int vendor_sample_ahb(void *sys, EGLDisplay display,
                             const AHardwareBuffer *ahb,
                             unsigned width,unsigned height,uint32_t format,
                             GLubyte pixel[4]) {
    void *gles=dlopen("/system/lib64/libGLESv2.so",RTLD_NOW|RTLD_LOCAL);
    if (!gles) return -1;
    EGLBoolean (*bind)(EGLenum)=dlsym(sys,"eglBindAPI");
    EGLBoolean (*choose)(EGLDisplay,const EGLint *,EGLConfig *,EGLint,EGLint *)=
        dlsym(sys,"eglChooseConfig");
    EGLContext (*create_ctx)(EGLDisplay,EGLConfig,EGLContext,const EGLint *)=
        dlsym(sys,"eglCreateContext");
    EGLSurface (*create_pb)(EGLDisplay,EGLConfig,const EGLint *)=
        dlsym(sys,"eglCreatePbufferSurface");
    EGLBoolean (*make)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=
        dlsym(sys,"eglMakeCurrent");
    EGLBoolean (*destroy_ctx)(EGLDisplay,EGLContext)=dlsym(sys,"eglDestroyContext");
    EGLBoolean (*destroy_pb)(EGLDisplay,EGLSurface)=dlsym(sys,"eglDestroySurface");
    EGLClientBuffer (*get_buf)(const AHardwareBuffer *)=
        dlsym(sys,"eglGetNativeClientBufferANDROID");
    EGLImageKHR (*create_img)(EGLDisplay,EGLContext,EGLenum,EGLClientBuffer,const EGLint *)=
        dlsym(sys,"eglCreateImageKHR");
    EGLBoolean (*destroy_img)(EGLDisplay,EGLImageKHR)=dlsym(sys,"eglDestroyImageKHR");
    void *(*proc)(const char *)=dlsym(sys,"eglGetProcAddress");
    void (*gen_tex)(GLsizei,GLuint *)=dlsym(gles,"glGenTextures");
    void (*bind_tex)(GLenum,GLuint)=dlsym(gles,"glBindTexture");
    void (*tex_param)(GLenum,GLenum,GLint)=dlsym(gles,"glTexParameteri");
    void (*delete_tex)(GLsizei,const GLuint *)=dlsym(gles,"glDeleteTextures");
    void (*gen_fbo)(GLsizei,GLuint *)=dlsym(gles,"glGenFramebuffers");
    void (*bind_fbo)(GLenum,GLuint)=dlsym(gles,"glBindFramebuffer");
    void (*attach)(GLenum,GLenum,GLenum,GLuint,GLint)=
        dlsym(gles,"glFramebufferTexture2D");
    GLenum (*check)(GLenum)=dlsym(gles,"glCheckFramebufferStatus");
    void (*read)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *)=
        dlsym(gles,"glReadPixels");
    GLenum (*error)(void)=dlsym(gles,"glGetError");
    void (*delete_fbo)(GLsizei,const GLuint *)=dlsym(gles,"glDeleteFramebuffers");
    if (!bind||!choose||!create_ctx||!create_pb||!make||!destroy_ctx||
        !destroy_pb||!get_buf||!create_img||!destroy_img||!proc||
        !gen_tex||!bind_tex||!tex_param||!delete_tex||!gen_fbo||
        !bind_fbo||!attach||!check||!read||!error||!delete_fbo) {
        dlclose(gles);return -2;
    }
    void (*image_tex)(GLenum,GLeglImageOES)=(void *)proc("glEGLImageTargetTexture2DOES");
    if (!image_tex) {dlclose(gles);return -2;}
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,
                    EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLint pba[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLConfig cfg=NULL;EGLint count=0;
    EGLContext ctx=EGL_NO_CONTEXT;
    EGLSurface surf=EGL_NO_SURFACE;
    EGLImageKHR img=EGL_NO_IMAGE_KHR;
    GLuint tex=0,fb=0;
    int result=-3;
    if (!bind(EGL_OPENGL_ES_API)||!choose(display,attrs,&cfg,1,&count)||!count)
        goto done;
    ctx=create_ctx(display,cfg,EGL_NO_CONTEXT,ca);
    surf=create_pb(display,cfg,pba);
    if(ctx==EGL_NO_CONTEXT||surf==EGL_NO_SURFACE||!make(display,surf,surf,ctx))
        goto done;
    img=create_img(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,get_buf(ahb),NULL);
    if(img==EGL_NO_IMAGE_KHR)goto done;
    gen_tex(1,&tex);bind_tex(GL_TEXTURE_2D,tex);
    tex_param(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    tex_param(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    image_tex(GL_TEXTURE_2D,(GLeglImageOES)img);
    gen_fbo(1,&fb);bind_fbo(GL_FRAMEBUFFER,fb);
    attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    if(check(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||error()!=GL_NO_ERROR)
        goto done;
    const unsigned points[5][2]={{8,8},{width/2,height/2},
        {width-8,height-8},{8,height-8},{width-8,8}};
    int matched=0;
    for(size_t i=0;i<5;i++) {
        GLubyte sample[4]={0};
        read((GLint)points[i][0],(GLint)points[i][1],1,1,
             GL_RGBA,GL_UNSIGNED_BYTE,sample);
        if(error()!=GL_NO_ERROR) break;
        if(i==0)memcpy(pixel,sample,4);
        printf("S15_TURNIP_AHB_VENDOR_PIXEL_%zu=%u,%u,%u,%u at=%u,%u\n",
               i,sample[0],sample[1],sample[2],sample[3],
               points[i][0],points[i][1]);
        bool rgba = sample[0]>=22&&sample[0]<=30 &&
                    sample[1]>=174&&sample[1]<=184 &&
                    sample[2]>=225&&sample[2]<=234 && sample[3]==255;
        bool bgra = sample[0]>=225&&sample[0]<=234 &&
                    sample[1]>=174&&sample[1]<=184 &&
                    sample[2]>=22&&sample[2]<=30 && sample[3]==255;
        if((format==5?bgra:rgba))matched++;
    }
    printf("S15_TURNIP_AHB_VENDOR_SAMPLE_POINTS=%d/5\n",matched);
    result=matched==5?0:-5;
done:
    if(fb)delete_fbo(1,&fb);
    if(tex)delete_tex(1,&tex);
    if(img!=EGL_NO_IMAGE_KHR)destroy_img(display,img);
    if(ctx!=EGL_NO_CONTEXT)make(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(surf!=EGL_NO_SURFACE)destroy_pb(display,surf);
    if(ctx!=EGL_NO_CONTEXT)destroy_ctx(display,ctx);
    dlclose(gles);
    return result;
}


static int test(void) {
    int ret=7;
    void *libnative=dlopen("/system/lib64/libnativewindow.so",RTLD_NOW|RTLD_LOCAL);
    void *egl=dlopen("/system/lib64/libEGL.so",RTLD_NOW|RTLD_LOCAL);
    if(!libnative||!egl) {puts("S15_NATIVE_LIBRARIES=UNAVAILABLE"); return 2;}
    int (*allocate)(const AHardwareBuffer_Desc*, AHardwareBuffer**)=
         dlsym(libnative,"AHardwareBuffer_allocate");
    void (*release)(AHardwareBuffer*)=dlsym(libnative,"AHardwareBuffer_release");
    void (*describe)(const AHardwareBuffer*,AHardwareBuffer_Desc*)=
         dlsym(libnative,"AHardwareBuffer_describe");
    EGLDisplay (*egl_get)(EGLNativeDisplayType)=dlsym(egl,"eglGetDisplay");
    EGLBoolean (*egl_init)(EGLDisplay,EGLint*,EGLint*)=dlsym(egl,"eglInitialize");
    EGLBoolean (*egl_term)(EGLDisplay)=dlsym(egl,"eglTerminate");
    if(!allocate||!release||!describe||!egl_get||!egl_init||!egl_term) {
        puts("S15_AHB_NDK=UNAVAILABLE");return 2;
    }
    uint32_t width=480,height=320;
    const char *requested=getenv("ALROOT_S15_AHB_RESOLUTION");
    if(requested && sscanf(requested,"%ux%u",&width,&height)!=2) {
        puts("S15_AHB_RESOLUTION=INVALID");return 2;
    }
    if(width<64||height<64||width>1920||height>1080)return 2;
    const char *requested_format=getenv("ALROOT_S15_AHB_FORMAT");
    uint32_t format=(requested_format && !strcmp(requested_format,"bgra8"))?
       5:AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    AHardwareBuffer_Desc desc={.width=width,.height=height,.layers=1,
       .format=format,
       .usage=AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER|
              AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
    AHardwareBuffer *ahb=NULL;
    if(allocate(&desc,&ahb)!=0||!ahb) {puts("S15_AHB_ALLOC=FAIL");return 3;}
    describe(ahb,&desc);
    printf("S15_AHB_DESC=%ux%u stride=%u format=%u\n",desc.width,desc.height,desc.stride,desc.format);
    uint64_t allocation_size=0;
    uint32_t stride=0;
    int imported_fd=desc.format==5?
       duplicate_bgra_image_fd(libnative,ahb,&allocation_size,&stride):
       alroot_kgsl_ahb_dup_image_fd(ahb,&allocation_size,&stride);
    printf("S15_AHB_IMAGE_FD_SELECT=%s bytes=%llu stride=%u\n",
           imported_fd>=0?"PASS":"FAIL",(unsigned long long)allocation_size,stride);
    if(imported_fd<0) goto cleanup_ahb;

    VkInstance inst=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkResult vr=vkCreateInstance(&ici,NULL,&inst);
    if(vr!=VK_SUCCESS){printf("S15_VK_INSTANCE=FAIL_%d\n",vr);goto cleanup_vulkan;}
    uint32_t np=0;
    vr=vkEnumeratePhysicalDevices(inst,&np,NULL);
    if(vr!=VK_SUCCESS||np!=1){printf("S15_VK_PHYSICAL_COUNT=%u err=%d\n",np,vr);goto cleanup_vulkan;}
    VkPhysicalDevice phys=VK_NULL_HANDLE;
    vr=vkEnumeratePhysicalDevices(inst,&np,&phys);
    if(vr!=VK_SUCCESS){printf("S15_VK_PHYSICAL=FAIL_%d\n",vr);goto cleanup_vulkan;}
    VkPhysicalDeviceProperties props={0};
    vkGetPhysicalDeviceProperties(phys,&props);
    printf("S15_VK_GPU=%s driver=%u\n",props.deviceName,props.driverVersion);
    if(!strstr(props.deviceName,"Adreno")){
        puts("S15_VK_GPU_EXPECTED_TURNIP_ADRENO=FAIL");goto cleanup_vulkan;
    }
    VkPhysicalDeviceExternalBufferInfo extinfo={
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
        .flags=0,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkExternalBufferProperties extprops={
        .sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
    vkGetPhysicalDeviceExternalBufferProperties(phys,&extinfo,&extprops);
    printf("S15_TURNIP_DMABUF_IMPORTABLE=%d compatible=0x%x features=0x%x\n",
       (extprops.externalMemoryProperties.externalMemoryFeatures&
        VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)!=0,
       extprops.externalMemoryProperties.compatibleHandleTypes,
       extprops.externalMemoryProperties.externalMemoryFeatures);
    if(!(extprops.externalMemoryProperties.externalMemoryFeatures&
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
        puts("S15_TURNIP_EXTERNAL_BUFFER_CAPS=REPORTED_UNSUPPORTED");
        /* Strict experimental isolation: diagnose whether the loader
         * advertises different caps than the actual Turnip import API.
         * Never use this opt-in in the real WSI without pixel validation. */
        if(!getenv("ALROOT_S15_DIAGNOSTIC_TRY_UNADVERTISED_IMPORT"))
            goto cleanup_vulkan;
        puts("S15_TURNIP_UNADVERTISED_IMPORT=DIAGNOSTIC_ONLY");
    }
    uint32_t nq=0,qfi=UINT32_MAX;
    vkGetPhysicalDeviceQueueFamilyProperties(phys,&nq,NULL);
    VkQueueFamilyProperties queues[32]={0};
    if(nq>32)nq=32;
    vkGetPhysicalDeviceQueueFamilyProperties(phys,&nq,queues);
    for(uint32_t i=0;i<nq;i++)if(queues[i].queueFlags&VK_QUEUE_TRANSFER_BIT){qfi=i;break;}
    if(qfi==UINT32_MAX)goto cleanup_vulkan;
    float prio=1.f;
    VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=qfi,.queueCount=1,.pQueuePriorities=&prio};
    const char *extensions[]={VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                               VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,
        .enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
    vr=vkCreateDevice(phys,&dci,NULL,&device);
    if(vr!=VK_SUCCESS){printf("S15_TURNIP_VK_DEVICE=FAIL_%d\n",vr);goto cleanup_vulkan;}
    PFN_vkGetMemoryFdPropertiesKHR getfd=(PFN_vkGetMemoryFdPropertiesKHR)
        vkGetDeviceProcAddr(device,"vkGetMemoryFdPropertiesKHR");
    if(!getfd){puts("S15_TURNIP_VK_FD_PROPERTIES=FUNCTION_MISSING");goto cleanup_vulkan;}
    VkExternalMemoryBufferCreateInfo external={
       .sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
       .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
       .pNext=&external,.size=allocation_size,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,
       .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    vr=vkCreateBuffer(device,&bci,NULL,&buffer);
    if(vr!=VK_SUCCESS){printf("S15_TURNIP_VK_BUFFER=FAIL_%d\n",vr);goto cleanup_vulkan;}
    VkMemoryFdPropertiesKHR fdprops={.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    vr=getfd(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,imported_fd,&fdprops);
    printf("S15_TURNIP_FD_MEMORY_TYPE_BITS=0x%x result=%d\n",fdprops.memoryTypeBits,vr);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    VkMemoryRequirements reqs={0};
    vkGetBufferMemoryRequirements(device,buffer,&reqs);
    printf("S15_TURNIP_VK_BUFFER_REQUIREMENTS=size:%llu align:%llu types:0x%x\n",
      (unsigned long long)reqs.size,(unsigned long long)reqs.alignment,reqs.memoryTypeBits);
    uint32_t valid=reqs.memoryTypeBits & fdprops.memoryTypeBits;
    if(!valid){puts("S15_TURNIP_BUFFER_MEMORY=NO_COMMON_TYPE");goto cleanup_vulkan;}
    uint32_t type=__builtin_ctz(valid);
    VkImportMemoryFdInfoKHR imp={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd=imported_fd};
    VkMemoryDedicatedAllocateInfo dedicated={
       .sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
       .pNext=&imp,.buffer=buffer};
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
       .pNext=&dedicated,.allocationSize=reqs.size,.memoryTypeIndex=type};
    vr=vkAllocateMemory(device,&ai,NULL,&memory);
    printf("S15_TURNIP_AHB_DMABUF_ALLOCATE=%d\n",vr);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    imported_fd=-1; /* successful external import takes the fd ownership */
    vr=vkBindBufferMemory(device,buffer,memory,0);
    printf("S15_TURNIP_AHB_DMABUF_BIND=%d\n",vr);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex=qfi};
    vr=vkCreateCommandPool(device,&pci,NULL,&pool);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    VkCommandBuffer cb=VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
       .commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,
       .commandBufferCount=1};
    vr=vkAllocateCommandBuffers(device,&cai,&cb);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vr=vkBeginCommandBuffer(cb,&begin);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    /* Little-endian RGBA bytes: R=0x1a, G=0xb2, B=0xe5, A=0xff. */
    vkCmdFillBuffer(cb,buffer,0,allocation_size,0xffe5b21a);
    vr=vkEndCommandBuffer(cb);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vr=vkCreateFence(device,&fci,NULL,&fence);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
       .commandBufferCount=1,.pCommandBuffers=&cb};
    VkQueue queue=VK_NULL_HANDLE;
    vkGetDeviceQueue(device,qfi,0,&queue);
    vr=vkQueueSubmit(queue,1,&submit,fence);
    printf("S15_TURNIP_AHB_GPU_FILL_SUBMIT=%d\n",vr);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    vr=vkWaitForFences(device,1,&fence,VK_TRUE,3000000000ull);
    printf("S15_TURNIP_AHB_GPU_FILL_FENCE=%d\n",vr);
    if(vr!=VK_SUCCESS)goto cleanup_vulkan;
    EGLDisplay display=egl_get(EGL_DEFAULT_DISPLAY);
    EGLint major=0,minor=0;
    if(display==EGL_NO_DISPLAY||!egl_init(display,&major,&minor)) {
       puts("S15_ANDROID_VENDOR_EGL=FAILED");goto cleanup_vulkan;
    }
    GLubyte pixel[4]={0};
    ret=vendor_sample_ahb(egl,display,ahb,desc.width,desc.height,desc.format,pixel);
    printf("S15_TURNIP_AHB_CROSS_DRIVER_PIXELS=%s sample=%u,%u,%u,%u\n",
       ret==0?"PASS":"FAIL",pixel[0],pixel[1],pixel[2],pixel[3]);
    egl_term(display);
cleanup_vulkan:
    if(device && device!=VK_NULL_HANDLE) {
       vkDeviceWaitIdle(device);
       if(fence)vkDestroyFence(device,fence,NULL);
       if(pool)vkDestroyCommandPool(device,pool,NULL);
       if(memory)vkFreeMemory(device,memory,NULL);
       if(buffer)vkDestroyBuffer(device,buffer,NULL);
       vkDestroyDevice(device,NULL);
    }
    if(inst)vkDestroyInstance(inst,NULL);
    if(imported_fd>=0)close(imported_fd);
cleanup_ahb:
    release(ahb);
    dlclose(libnative);dlclose(egl);
    return ret;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    return test();
}
