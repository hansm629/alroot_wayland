/* Android-vendor-neutral AHB transport -> VkImage import test.
 *
 * Allocate with public NDK AHardwareBuffer, send over a UNIX socket between
 * two independent processes, receive in the child, then use the real system
 * Android Vulkan loader (not Termux Turnip/wrapper ICD) to import an image.
 *
 * This is a GPU-buffer import test ONLY. It does not read/copy pixels,
 * submit frames to Wayland, perform cross-API synchronization, or scan out.
 * Do not report Wayland GPU-copy PASS based on this test alone.
 */
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "alroot_vulkan_ahb_producer.h"

/* BGRA8 is an optional Android gralloc format (value 5), not guaranteed
 * by every public NDK revision or every GPU. Test allocation and Vulkan
 * import before advertising it in a compositor's protocol capabilities. */
#define ALROOT_AHB_BGRA8_FORMAT 5u

typedef int (*ahb_allocate_fn)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
typedef int (*ahb_send_fn)(const AHardwareBuffer *, int);
typedef int (*ahb_recv_fn)(int, AHardwareBuffer **);
typedef void (*ahb_release_fn)(AHardwareBuffer *);
typedef void (*ahb_describe_fn)(const AHardwareBuffer *, AHardwareBuffer_Desc *);

struct native_api {
    void *lib;
    ahb_allocate_fn allocate;
    ahb_send_fn send;
    ahb_recv_fn receive;
    ahb_release_fn release;
    ahb_describe_fn describe;
};

static int native_load(struct native_api *api) {
    api->lib = dlopen("/system/lib64/libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
    if (!api->lib) return -1;
    *(void **)&api->allocate = dlsym(api->lib, "AHardwareBuffer_allocate");
    *(void **)&api->send = dlsym(api->lib, "AHardwareBuffer_sendHandleToUnixSocket");
    *(void **)&api->receive = dlsym(api->lib, "AHardwareBuffer_recvHandleFromUnixSocket");
    *(void **)&api->release = dlsym(api->lib, "AHardwareBuffer_release");
    *(void **)&api->describe = dlsym(api->lib, "AHardwareBuffer_describe");
    return api->allocate && api->send && api->receive &&
           api->release && api->describe ? 0 : -1;
}

static int submit_gpu_clear(PFN_vkGetDeviceProcAddr gdpa, VkDevice device,
                            uint32_t family, VkImage img) {
#define DEVFN(name) PFN_##name name##_fn=(PFN_##name)gdpa(device,#name)
    DEVFN(vkGetDeviceQueue);
    DEVFN(vkCreateCommandPool);
    DEVFN(vkAllocateCommandBuffers);
    DEVFN(vkBeginCommandBuffer);
    DEVFN(vkCmdPipelineBarrier);
    DEVFN(vkCmdClearColorImage);
    DEVFN(vkEndCommandBuffer);
    DEVFN(vkQueueSubmit);
    DEVFN(vkCreateFence);
    DEVFN(vkWaitForFences);
    DEVFN(vkDestroyFence);
    DEVFN(vkDestroyCommandPool);
#undef DEVFN
    if (!vkGetDeviceQueue_fn || !vkCreateCommandPool_fn ||
        !vkAllocateCommandBuffers_fn || !vkBeginCommandBuffer_fn ||
        !vkCmdPipelineBarrier_fn || !vkCmdClearColorImage_fn ||
        !vkEndCommandBuffer_fn || !vkQueueSubmit_fn || !vkCreateFence_fn ||
        !vkWaitForFences_fn || !vkDestroyFence_fn || !vkDestroyCommandPool_fn) {
        puts("VK_AHB_CLEAR=DEVICE_SYMBOL_MISSING");
        return -1;
    }
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pci={
        .sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex=family
    };
    if (vkCreateCommandPool_fn(device,&pci,NULL,&pool)!=VK_SUCCESS)
        return -1;
    VkCommandBuffer cb=VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo a={
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount=1
    };
    int ok=0;
    VkFence fence=VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers_fn(device,&a,&cb)!=VK_SUCCESS) goto cleanup;
    VkCommandBufferBeginInfo begin={
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    if (vkBeginCommandBuffer_fn(cb,&begin)!=VK_SUCCESS) goto cleanup;
    VkImageSubresourceRange sub={ .aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount=1, .layerCount=1 };
    VkImageMemoryBarrier before={
        .sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=img, .subresourceRange=sub
    };
    vkCmdPipelineBarrier_fn(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,
                           0,NULL,1,&before);
    VkClearColorValue color={ .float32={0.11f,0.72f,0.91f,1.0f} };
    vkCmdClearColorImage_fn(cb,img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            &color,1,&sub);
    VkImageMemoryBarrier after={
        .sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout=VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=img, .subresourceRange=sub
    };
    vkCmdPipelineBarrier_fn(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,
                           0,NULL,1,&after);
    if (vkEndCommandBuffer_fn(cb)!=VK_SUCCESS) goto cleanup;
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence_fn(device,&fi,NULL,&fence)!=VK_SUCCESS) goto cleanup;
    VkQueue queue=VK_NULL_HANDLE;
    vkGetDeviceQueue_fn(device,family,0,&queue);
    VkSubmitInfo submit={
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&cb
    };
    if (vkQueueSubmit_fn(queue,1,&submit,fence)!=VK_SUCCESS) goto cleanup;
    if (vkWaitForFences_fn(device,1,&fence,VK_TRUE,3000000000ULL)!=VK_SUCCESS)
        goto cleanup;
    ok=1;
    puts("VK_AHB_CLEAR=GPU_SUBMITTED_AND_COMPLETED");
    puts("VK_AHB_CLEAR_CPU_PIXEL_COPY=0");
cleanup:
    if (fence) vkDestroyFence_fn(device,fence,NULL);
    vkDestroyCommandPool_fn(device,pool,NULL);
    return ok ? 0 : -1;
}

int alroot_ahb_vulkan_import_and_clear(AHardwareBuffer *buf, int clear) {
    int ok = 0;
    /* Select system Android Vulkan (host) by default, or explicitly select
     * Termux Vulkan loader plus a caller-scoped VK_DRIVER_FILES ICD (client).
     * Never change global ICD files or Termux-X11 DRI3 configuration. */
    const char *loader = getenv("ALROOT_TEST_VULKAN_LOADER");
    if (!loader || !*loader) loader = "/system/lib64/libvulkan.so";
    void *vk = dlopen(loader, RTLD_NOW | RTLD_LOCAL);
    if (!vk) {
        puts("VK_AHB_IMPORT=NO_VULKAN_LOADER");
        return -1;
    }
    PFN_vkGetInstanceProcAddr gpa =
        (PFN_vkGetInstanceProcAddr)dlsym(vk, "vkGetInstanceProcAddr");
    if (!gpa) { puts("VK_AHB_IMPORT=NO_GPA"); goto done; }

    PFN_vkCreateInstance ci = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
    if (!ci) goto done;
    VkInstance inst = VK_NULL_HANDLE;
    VkApplicationInfo app = { .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName="alroot-gpu-stage1-ahb-probe",
                              .apiVersion=VK_API_VERSION_1_1 };
    VkInstanceCreateInfo info={ .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                .pApplicationInfo=&app };
    VkResult res=ci(&info,NULL,&inst);
    if (res!=VK_SUCCESS || !inst) {
        printf("VK_AHB_IMPORT=CREATE_INSTANCE_FAILED code=%d\n",(int)res);
        goto done;
    }
    PFN_vkDestroyInstance destroy_inst=
        (PFN_vkDestroyInstance)gpa(inst,"vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices enumerate=
        (PFN_vkEnumeratePhysicalDevices)gpa(inst,"vkEnumeratePhysicalDevices");
    PFN_vkEnumerateDeviceExtensionProperties extensions=
        (PFN_vkEnumerateDeviceExtensionProperties)gpa(inst,"vkEnumerateDeviceExtensionProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queues=
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gpa(inst,"vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceMemoryProperties memory_props=
        (PFN_vkGetPhysicalDeviceMemoryProperties)gpa(inst,"vkGetPhysicalDeviceMemoryProperties");
    PFN_vkCreateDevice create_dev=(PFN_vkCreateDevice)gpa(inst,"vkCreateDevice");
    PFN_vkGetDeviceProcAddr gdpa=(PFN_vkGetDeviceProcAddr)gpa(inst,"vkGetDeviceProcAddr");
    if (!destroy_inst || !enumerate || !extensions || !get_queues ||
        !memory_props || !create_dev || !gdpa) {
        puts("VK_AHB_IMPORT=INSTANCE_FUNCTION_MISSING");
        destroy_inst(inst,NULL);
        goto done;
    }

    uint32_t count=0;
    res=enumerate(inst,&count,NULL);
    VkPhysicalDevice physical[8]={0};
    if (res!=VK_SUCCESS || !count) {
        puts("VK_AHB_IMPORT=NO_GPU");
        destroy_inst(inst,NULL);
        goto done;
    }
    if (count>8) count=8;
    res=enumerate(inst,&count,physical);
    VkPhysicalDevice selected=VK_NULL_HANDLE;
    uint32_t family=0;
    for (uint32_t dev=0;dev<count && !selected;dev++) {
        uint32_t ne=0;
        if (extensions(physical[dev],NULL,&ne,NULL)!=VK_SUCCESS || !ne) continue;
        VkExtensionProperties *ext=calloc(ne,sizeof(*ext));
        if (!ext) continue;
        int ahb=0, external_fd=0, dma_buf=0, drm_modifiers=0;
        if (extensions(physical[dev],NULL,&ne,ext)==VK_SUCCESS) {
            for (uint32_t i=0;i<ne;i++) {
                if (!strcmp(ext[i].extensionName,
                            VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME))
                    ahb=1;
                else if (!strcmp(ext[i].extensionName,
                                 VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
                    external_fd=1;
                else if (!strcmp(ext[i].extensionName,
                                 VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME))
                    dma_buf=1;
                else if (!strcmp(ext[i].extensionName,
                                 VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME))
                    drm_modifiers=1;
            }
        }
        printf("VK_AHB_CAPS device=%u ahb=%d external_fd=%d dma_buf=%d drm_modifier=%d\n",
               dev,ahb,external_fd,dma_buf,drm_modifiers);
        free(ext);
        if (!ahb) continue;
        uint32_t nf=0;
        get_queues(physical[dev],&nf,NULL);
        if (nf>32) nf=32;
        VkQueueFamilyProperties families[32]={0};
        if (!nf) continue;
        get_queues(physical[dev],&nf,families);
        for (uint32_t j=0;j<nf;j++)
            if (families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                selected=physical[dev];
                family=j;
                break;
            }
    }
    if (!selected) {
        puts("VK_AHB_IMPORT=NO_AHB_CAPABLE_GPU");
        destroy_inst(inst,NULL);
        goto done;
    }
    float prio=1.f;
    VkDeviceQueueCreateInfo qci={
        .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&prio
    };
    const char *devext[]={
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME
    };
    VkDeviceCreateInfo dci={
        .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1, .pQueueCreateInfos=&qci,
        .enabledExtensionCount=1, .ppEnabledExtensionNames=devext
    };
    VkDevice device=VK_NULL_HANDLE;
    res=create_dev(selected,&dci,NULL,&device);
    if (res!=VK_SUCCESS || !device) {
        printf("VK_AHB_IMPORT=CREATE_DEVICE_FAILED code=%d\n",(int)res);
        destroy_inst(inst,NULL);
        goto done;
    }
#define VKDEV(symbol) PFN_##symbol symbol##_fn=(PFN_##symbol)gdpa(device,#symbol)
    VKDEV(vkGetAndroidHardwareBufferPropertiesANDROID);
    VKDEV(vkCreateImage);
    VKDEV(vkGetImageMemoryRequirements);
    VKDEV(vkAllocateMemory);
    VKDEV(vkBindImageMemory);
    VKDEV(vkDestroyImage);
    VKDEV(vkFreeMemory);
    VKDEV(vkDestroyDevice);
#undef VKDEV
    if (!vkGetAndroidHardwareBufferPropertiesANDROID_fn || !vkCreateImage_fn ||
        !vkGetImageMemoryRequirements_fn || !vkAllocateMemory_fn ||
        !vkBindImageMemory_fn || !vkDestroyImage_fn || !vkFreeMemory_fn ||
        !vkDestroyDevice_fn) {
        puts("VK_AHB_IMPORT=DEVICE_FUNCTION_MISSING");
        if (vkDestroyDevice_fn) vkDestroyDevice_fn(device,NULL);
        destroy_inst(inst,NULL);
        goto done;
    }
    VkAndroidHardwareBufferFormatPropertiesANDROID fmt={
        .sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID
    };
    VkAndroidHardwareBufferPropertiesANDROID props={
        .sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
        .pNext=&fmt
    };
    res=vkGetAndroidHardwareBufferPropertiesANDROID_fn(device,buf,&props);
    if (res!=VK_SUCCESS || fmt.format==VK_FORMAT_UNDEFINED) {
        printf("VK_AHB_IMPORT=PROPERTIES_FAILED code=%d fmt=%d\n",
               (int)res,(int)fmt.format);
    } else {
        VkExternalMemoryImageCreateInfo ext={
            .sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID
        };
        VkImageCreateInfo imginfo={
            .sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext=&ext,
            .imageType=VK_IMAGE_TYPE_2D,
            .format=fmt.format,
            .extent={64,64,1},
            .mipLevels=1, .arrayLayers=1, .samples=VK_SAMPLE_COUNT_1_BIT,
            .tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
                   VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED
        };
        VkImage img=VK_NULL_HANDLE;
        res=vkCreateImage_fn(device,&imginfo,NULL,&img);
        if (res==VK_SUCCESS && img) {
            VkMemoryRequirements req={0};
            vkGetImageMemoryRequirements_fn(device,img,&req);
            VkPhysicalDeviceMemoryProperties mprops={0};
            memory_props(selected,&mprops);
            uint32_t mask=req.memoryTypeBits&props.memoryTypeBits;
            int index=-1;
            for (uint32_t i=0;i<mprops.memoryTypeCount;i++)
                if (mask&(1u<<i)) { index=(int)i; break; }
            if (index>=0) {
                VkImportAndroidHardwareBufferInfoANDROID import={
                    .sType=VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
                    .buffer=buf
                };
                VkMemoryDedicatedAllocateInfo dedicated={
                    .sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                    .pNext=&import, .image=img
                };
                VkMemoryAllocateInfo meminfo={
                    .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext=&dedicated,
                    .allocationSize=props.allocationSize,
                    .memoryTypeIndex=(uint32_t)index
                };
                VkDeviceMemory mem=VK_NULL_HANDLE;
                res=vkAllocateMemory_fn(device,&meminfo,NULL,&mem);
                if (res==VK_SUCCESS && mem) {
                    res=vkBindImageMemory_fn(device,img,mem,0);
                    if (res==VK_SUCCESS) {
                        puts("VK_AHB_IMPORT=PASS");
                        puts("VK_AHB_IMPORT_PIXEL_TRANSFER=NOT_TESTED");
                        puts("VK_AHB_IMPORT_WAYLAND_PRESENT=NOT_TESTED");
                        ok=(!clear || submit_gpu_clear(gdpa,device,family,img)==0);
                    } else printf("VK_AHB_IMPORT=BIND_FAILED code=%d\n",(int)res);
                    vkFreeMemory_fn(device,mem,NULL);
                } else printf("VK_AHB_IMPORT=ALLOCATE_FAILED code=%d\n",(int)res);
            } else puts("VK_AHB_IMPORT=MEMORY_TYPES_INCOMPATIBLE");
            vkDestroyImage_fn(device,img,NULL);
        } else printf("VK_AHB_IMPORT=CREATE_IMAGE_FAILED code=%d\n",(int)res);
    }
    vkDestroyDevice_fn(device,NULL);
    destroy_inst(inst,NULL);
done:
    dlclose(vk);
    return ok ? 0 : -1;
}

#ifndef ALROOT_VK_PRODUCER_ONLY
int main(void) {
    struct native_api api={0};
    if (native_load(&api)) {
        puts("AHB_IPC=NO_NATIVE_API");
        return 2;
    }
    AHardwareBuffer_Desc request={0};
    request.width=64; request.height=64; request.layers=1;
    request.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    const char *requested_format=getenv("ALROOT_TEST_AHB_FORMAT");
    if (requested_format && !strcmp(requested_format,"bgra8"))
        request.format=ALROOT_AHB_BGRA8_FORMAT;
    printf("AHB_TEST_FORMAT=%s\n",
           request.format==ALROOT_AHB_BGRA8_FORMAT?"BGRA8":"RGBA8");
    request.usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer *parent_buf=NULL;
    if (api.allocate(&request,&parent_buf) || !parent_buf) {
        puts("AHB_IPC=ALLOC_FAILED");
        return 3;
    }
    int sockets[2];
    if (socketpair(AF_UNIX,SOCK_STREAM,0,sockets)!=0) {
        puts("AHB_IPC=SOCKET_FAILED");
        api.release(parent_buf);
        return 3;
    }
    pid_t child=fork();
    if (child==-1) {
        puts("AHB_IPC=FORK_FAILED");
        close(sockets[0]);close(sockets[1]);api.release(parent_buf);
        return 3;
    }
    if (child==0) {
        alarm(8);
        close(sockets[0]);
        AHardwareBuffer *received=NULL;
        int status=0;
        if (!api.receive(sockets[1],&received) && received) {
            AHardwareBuffer_Desc desc={0};
            api.describe(received,&desc);
            if (desc.width==request.width && desc.height==request.height &&
                desc.format==request.format) {
                puts("AHB_IPC_RECEIVE=PASS");
                status=alroot_ahb_vulkan_import_and_clear(received,
                           getenv("ALROOT_TEST_GPU_CLEAR") != NULL)==0 ? 0 : 1;
            } else { puts("AHB_IPC_RECEIVE=BAD_METADATA"); status=1; }
            api.release(received);
        } else { puts("AHB_IPC_RECEIVE=FAILED"); status=1; }
        close(sockets[1]);
        fflush(stdout);
        _exit(status);
    }
    close(sockets[1]);
    int sent=api.send(parent_buf,sockets[0]);
    close(sockets[0]);
    api.release(parent_buf);
    int result=0;
    waitpid(child,&result,0);
    if (!sent) puts("AHB_IPC_SEND=PASS");
    else puts("AHB_IPC_SEND=FAILED");
    printf("AHB_IPC_CHILD_EXIT=%d\n",WIFEXITED(result)?WEXITSTATUS(result):-1);
    dlclose(api.lib);
    return sent || !WIFEXITED(result) || WEXITSTATUS(result) ? 4 : 0;
}
#endif
