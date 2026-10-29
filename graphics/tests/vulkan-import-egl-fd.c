#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>
#include "alroot_vulkan_import_fd.h"
#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int try_import(VkPhysicalDevice phys, VkDevice device,
                       PFN_vkGetDeviceProcAddr get_dev, int fd,
                       VkFormat format, int width, int height,
                       VkExternalMemoryHandleTypeFlagBits handle,
                       VkImageTiling tiling, const char *name) {
#define FN(sym) PFN_##sym sym##_fn=(PFN_##sym)get_dev(device,#sym)
    FN(vkCreateImage); FN(vkGetImageMemoryRequirements);
    FN(vkGetMemoryFdPropertiesKHR); FN(vkAllocateMemory);
    FN(vkBindImageMemory); FN(vkDestroyImage); FN(vkFreeMemory);
#undef FN
    if (!vkCreateImage_fn || !vkGetImageMemoryRequirements_fn ||
        !vkGetMemoryFdPropertiesKHR_fn || !vkAllocateMemory_fn ||
        !vkBindImageMemory_fn || !vkDestroyImage_fn || !vkFreeMemory_fn)
        return -1;
    (void)phys;
    VkExternalMemoryImageCreateInfo external={
        .sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes=handle
    };
    VkImageCreateInfo ici={
        .sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext=&external,
        .imageType=VK_IMAGE_TYPE_2D, .format=format,
        .extent={(uint32_t)width,(uint32_t)height,1},
        .mipLevels=1, .arrayLayers=1, .samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=tiling, .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|
                             VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED
    };
    VkImage img=VK_NULL_HANDLE;
    VkResult res=vkCreateImage_fn(device,&ici,NULL,&img);
    if (res!=VK_SUCCESS) {
        printf("OPAQUE_FD_PROBE_%s=CREATE_IMAGE_ERROR_%d\n",name,(int)res);
        return -1;
    }
    int copy=dup(fd);
    if(copy<0) { vkDestroyImage_fn(device,img,NULL); return -1; }
    VkMemoryFdPropertiesKHR fdprops={
        .sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR
    };
    res=vkGetMemoryFdPropertiesKHR_fn(device,handle,copy,&fdprops);
    if (res!=VK_SUCCESS) {
        printf("OPAQUE_FD_PROBE_%s=FD_PROPERTIES_ERROR_%d\n",name,(int)res);
        close(copy);
        vkDestroyImage_fn(device,img,NULL);
        return -1;
    }
    VkMemoryRequirements req={0};
    vkGetImageMemoryRequirements_fn(device,img,&req);
    uint32_t mask=req.memoryTypeBits & fdprops.memoryTypeBits;
    int idx=-1;
    for(int i=0;i<32;i++) if(mask&(1u<<i)) {idx=i;break;}
    if(idx<0) {
        printf("OPAQUE_FD_PROBE_%s=NO_MEMORY_TYPE\n",name);
        close(copy);
        vkDestroyImage_fn(device,img,NULL);
        return -1;
    }
    VkImportMemoryFdInfoKHR imp={
        .sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType=handle, .fd=copy
    };
    VkMemoryDedicatedAllocateInfo dedicated={
        .sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext=&imp, .image=img
    };
    VkMemoryAllocateInfo mai={
        .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext=&dedicated, .allocationSize=req.size,
        .memoryTypeIndex=(uint32_t)idx
    };
    VkDeviceMemory mem=VK_NULL_HANDLE;
    res=vkAllocateMemory_fn(device,&mai,NULL,&mem);
    if (res!=VK_SUCCESS) {
        printf("OPAQUE_FD_PROBE_%s=ALLOCATE_ERROR_%d\n",name,(int)res);
        close(copy);
        vkDestroyImage_fn(device,img,NULL);
        return -1;
    }
    res=vkBindImageMemory_fn(device,img,mem,0);
    printf("OPAQUE_FD_PROBE_%s=%s\n",name,
           res==VK_SUCCESS ? "IMPORT_BIND_PASS" : "BIND_FAILED");
    vkFreeMemory_fn(device,mem,NULL);
    vkDestroyImage_fn(device,img,NULL);
    return res==VK_SUCCESS ? 0 : -1;
}

int alroot_probe_vulkan_import_fd(int fd, uint32_t drm_fourcc,
                                  int width, int height) {
    const VkFormat fmt=drm_fourcc==0x34324241 ?
                       VK_FORMAT_R8G8B8A8_UNORM :
                       drm_fourcc==0x34325241 ?
                       VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_UNDEFINED;
    if (fmt==VK_FORMAT_UNDEFINED || width<1 || height<1 || fd<0) {
        puts("OPAQUE_FD_PROBE=BAD_FORMAT_OR_FD");
        return -1;
    }
    void *vulkan=dlopen("/system/lib64/libvulkan.so",RTLD_NOW|RTLD_LOCAL);
    if(!vulkan) return -1;
    PFN_vkGetInstanceProcAddr gp=(PFN_vkGetInstanceProcAddr)
        dlsym(vulkan,"vkGetInstanceProcAddr");
    if(!gp) { dlclose(vulkan); return -1; }
    PFN_vkCreateInstance create=(PFN_vkCreateInstance)gp(NULL,"vkCreateInstance");
    VkInstance instance=VK_NULL_HANDLE;
    VkApplicationInfo app={
        .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="alroot-opaque-fd-experiment",
        .apiVersion=VK_API_VERSION_1_1
    };
    VkInstanceCreateInfo ci={
        .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo=&app
    };
    if (!create || create(&ci,NULL,&instance)!=VK_SUCCESS) {
        dlclose(vulkan);
        return -1;
    }
#define INST(sym) PFN_##sym sym##_fn=(PFN_##sym)gp(instance,#sym)
    INST(vkDestroyInstance); INST(vkEnumeratePhysicalDevices);
    INST(vkEnumerateDeviceExtensionProperties);
    INST(vkGetPhysicalDeviceQueueFamilyProperties);
    INST(vkCreateDevice); INST(vkGetDeviceProcAddr);
#undef INST
    int passed=0;
    if (vkDestroyInstance_fn && vkEnumeratePhysicalDevices_fn &&
        vkEnumerateDeviceExtensionProperties_fn &&
        vkGetPhysicalDeviceQueueFamilyProperties_fn &&
        vkCreateDevice_fn && vkGetDeviceProcAddr_fn) {
        uint32_t count=1;
        VkPhysicalDevice phys=VK_NULL_HANDLE;
        if(vkEnumeratePhysicalDevices_fn(instance,&count,&phys)==VK_SUCCESS && count) {
            uint32_t nf=0,family=UINT32_MAX;
            vkGetPhysicalDeviceQueueFamilyProperties_fn(phys,&nf,NULL);
            VkQueueFamilyProperties queues[32]={0};
            if(nf>32) nf=32;
            vkGetPhysicalDeviceQueueFamilyProperties_fn(phys,&nf,queues);
            for(uint32_t j=0;j<nf;j++)
                if(queues[j].queueFlags&VK_QUEUE_GRAPHICS_BIT) { family=j;break; }
            if(family!=UINT32_MAX) {
                float prio=1.f;
                VkDeviceQueueCreateInfo q={
                    .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&prio
                };
                const char *exts[]={
                    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
                };
                VkDeviceCreateInfo dc={
                    .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                    .queueCreateInfoCount=1,.pQueueCreateInfos=&q,
                    .enabledExtensionCount=1,.ppEnabledExtensionNames=exts
                };
                VkDevice dev=VK_NULL_HANDLE;
                VkResult result=vkCreateDevice_fn(phys,&dc,NULL,&dev);
                if(result==VK_SUCCESS && dev) {
                    const struct {
                        VkExternalMemoryHandleTypeFlagBits handle;
                        VkImageTiling tiling; const char *name;
                    } candidates[]={
                        {VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
                         VK_IMAGE_TILING_OPTIMAL,"OPAQUE_OPTIMAL"},
                        {VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
                         VK_IMAGE_TILING_LINEAR,"OPAQUE_LINEAR"},
                        {VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                         VK_IMAGE_TILING_OPTIMAL,"DMABUF_OPTIMAL"},
                        {VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                         VK_IMAGE_TILING_LINEAR,"DMABUF_LINEAR"}
                    };
                    for(size_t i=0;i<4;i++)
                        if(!try_import(phys,dev,vkGetDeviceProcAddr_fn,fd,
                                       fmt,width,height,candidates[i].handle,
                                       candidates[i].tiling,candidates[i].name))
                            passed=1;
                    PFN_vkDestroyDevice destroy=(PFN_vkDestroyDevice)
                        vkGetDeviceProcAddr_fn(dev,"vkDestroyDevice");
                    if(destroy)destroy(dev,NULL);
                } else printf("OPAQUE_FD_PROBE=VK_DEVICE_ERROR_%d\n",(int)result);
            }
        }
    }
    if(vkDestroyInstance_fn)vkDestroyInstance_fn(instance,NULL);
    dlclose(vulkan);
    return passed ? 0 : -1;
}
