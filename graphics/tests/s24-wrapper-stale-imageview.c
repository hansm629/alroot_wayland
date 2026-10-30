/* S24 diagnostic: intentionally request an ImageView on a destroyed image.
 * This is invalid Vulkan usage, tested ONLY against the *private* patched
 * wrapper to confirm it returns a safe error rather than invoking a vendor
 * driver with a stale image handle. No X server, surface or display touched.
 */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    uint32_t num_gpus = 1;
    VkInstanceCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkResult rc = vkCreateInstance(&ci, NULL, &instance);
    if (rc != VK_SUCCESS) {
        printf("S24_STALE_IMAGE_TEST_INSTANCE=FAIL rc:%d\n",rc);
        return 2;
    }
    if (vkEnumeratePhysicalDevices(instance, &num_gpus, &gpu) != VK_SUCCESS ||
        num_gpus != 1 || !gpu) {
        printf("S24_STALE_IMAGE_TEST_PHYSICAL_GPU=MISSING\n");
        vkDestroyInstance(instance,NULL);
        return 3;
    }
    uint32_t count=0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,NULL);
    if (count==0 || count>128) return 3;
    VkQueueFamilyProperties *families=calloc(count,sizeof(*families));
    if (!families) return 3;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families);
    uint32_t family=UINT32_MAX;
    for (uint32_t i=0;i<count;i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {family=i;break;}
    }
    free(families);
    if (family==UINT32_MAX) return 3;
    float priority=1.f;
    VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount=1,.pQueueCreateInfos=&queue};
    rc=vkCreateDevice(gpu,&di,NULL,&device);
    if (rc!=VK_SUCCESS) {
        printf("S24_STALE_IMAGE_TEST_DEVICE=FAIL rc:%d\n",rc);
        vkDestroyInstance(instance,NULL);
        return 4;
    }

    VkImageCreateInfo image_info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
        .extent={64,64,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    rc=vkCreateImage(device,&image_info,NULL,&image);
    if (rc!=VK_SUCCESS || !image) {
        printf("S24_STALE_IMAGE_TEST_CREATE=FAIL rc:%d\n",rc);
        vkDestroyDevice(device,NULL);
        vkDestroyInstance(instance,NULL);
        return 5;
    }
    VkImage stale=image;
    vkDestroyImage(device,image,NULL);

    VkImageViewCreateInfo view_info={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=stale,.viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view=VK_NULL_HANDLE;
    rc=vkCreateImageView(device,&view_info,NULL,&view);
    printf("S24_STALE_IMAGE_VIEW_RETURN=%d\n",rc);
    if (rc==VK_SUCCESS && view) vkDestroyImageView(device,view,NULL);
    vkDestroyDevice(device,NULL);
    vkDestroyInstance(instance,NULL);
    if (rc != VK_ERROR_INITIALIZATION_FAILED) {
        printf("S24_STALE_IMAGE_VIEW_GUARD=FAIL_UNEXPECTED_RETURN\n");
        return 6;
    }
    puts("S24_STALE_IMAGE_VIEW_GUARD=PASS_CLEAN_ERROR_NO_VENDOR_CALL");
    return 0;
}
