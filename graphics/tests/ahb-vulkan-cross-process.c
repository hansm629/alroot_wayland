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

static int import_vulkan_image(AHardwareBuffer *buf) {
    int ok = 0;
    void *vk = dlopen("/system/lib64/libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!vk) {
        puts("VK_AHB_IMPORT=NO_SYSTEM_VULKAN");
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
        int ahb=0;
        if (extensions(physical[dev],NULL,&ne,ext)==VK_SUCCESS)
            for (uint32_t i=0;i<ne;i++)
                if (!strcmp(ext[i].extensionName,
                            VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME))
                    ahb=1;
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
            .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
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
                        ok=1;
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

int main(void) {
    struct native_api api={0};
    if (native_load(&api)) {
        puts("AHB_IPC=NO_NATIVE_API");
        return 2;
    }
    AHardwareBuffer_Desc request={0};
    request.width=64; request.height=64; request.layers=1;
    request.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
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
                status=import_vulkan_image(received)==0 ? 0 : 1;
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
