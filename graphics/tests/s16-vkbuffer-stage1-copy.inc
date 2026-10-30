/* Diagnostic mirror of Xwayland S1.6 host's VkBuffer -> VkImage GPU-copy.
 * No CPU pixel upload.  Transfer output copied ONLY to a tiny readback buffer
 * for the 5-pixel acceptance test.  This is a one-shot disposable consumer. */
static int s16_gpu_copy_probe(int fd,const struct header*h){
 const int use_sync_file=getenv("S16_GPU_SYNC_FD_WAIT")!=NULL;
 int result=-1;
 void*lib=dlopen("/system/lib64/libvulkan.so",RTLD_NOW|RTLD_LOCAL);
 if(!lib)return -1;
 PFN_vkGetInstanceProcAddr gip=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vkGetInstanceProcAddr");
 if(!gip)return -1;
 PFN_vkCreateInstance createInstance=(PFN_vkCreateInstance)gip(NULL,"vkCreateInstance");
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Alroot S16 Buffer GPU image copy proof",.apiVersion=VK_API_VERSION_1_2};
 VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
 VkInstance ins=VK_NULL_HANDLE;VkResult r=createInstance(&ii,NULL,&ins);
 printf("S16_COPY_VK_INSTANCE_RC=%d\n",r);if(r)return -1;
#define L(n) PFN_vk##n n##_fn=(PFN_vk##n)gip(ins,"vk" #n)
 L(EnumeratePhysicalDevices);L(GetPhysicalDeviceQueueFamilyProperties);L(GetPhysicalDeviceMemoryProperties);
 L(GetPhysicalDeviceExternalBufferProperties);L(CreateDevice);L(GetDeviceProcAddr);L(DestroyInstance);
#undef L
 uint32_t np=1;VkPhysicalDevice phy=VK_NULL_HANDLE;
 if(EnumeratePhysicalDevices_fn(ins,&np,&phy)!=VK_SUCCESS||np!=1)goto cleanup_instance;
 uint32_t nq=0;GetPhysicalDeviceQueueFamilyProperties_fn(phy,&nq,NULL);
 VkQueueFamilyProperties family[16];if(nq>16)nq=16;
 GetPhysicalDeviceQueueFamilyProperties_fn(phy,&nq,family);
 uint32_t q=UINT32_MAX;for(uint32_t i=0;i<nq;i++)if(family[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){q=i;break;}
 if(q==UINT32_MAX)goto cleanup_instance;
 VkPhysicalDeviceExternalBufferInfo ei={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
 VkExternalBufferProperties ep={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
 GetPhysicalDeviceExternalBufferProperties_fn(phy,&ei,&ep);
 printf("S16_COPY_DMA_BUF_EXTERNAL_FEATURES=0x%x\n",ep.externalMemoryProperties.externalMemoryFeatures);
 if(!(ep.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))goto cleanup_instance;
 float priority=1.f;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=q,.queueCount=1,.pQueuePriorities=&priority};
 const char *extensions[]={"VK_KHR_external_memory_fd","VK_EXT_external_memory_dma_buf","VK_KHR_external_semaphore_fd"};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=use_sync_file?3u:2u,.ppEnabledExtensionNames=extensions};
 VkDevice dev=VK_NULL_HANDLE;r=CreateDevice_fn(phy,&di,NULL,&dev);if(r){printf("S16_COPY_VK_DEVICE_RC=%d\n",r);goto cleanup_instance;}
 PFN_vkGetDeviceProcAddr gd=(PFN_vkGetDeviceProcAddr)GetDeviceProcAddr_fn;
#define P(n) PFN_vk##n n##_fn=(PFN_vk##n)gd(dev,"vk" #n)
 P(CreateBuffer);P(DestroyBuffer);P(GetBufferMemoryRequirements);P(GetBufferMemoryRequirements2);
 P(CreateImage);P(DestroyImage);P(GetImageMemoryRequirements);P(AllocateMemory);P(FreeMemory);P(BindBufferMemory);P(BindImageMemory);
 P(GetMemoryFdPropertiesKHR);P(CreateCommandPool);P(DestroyCommandPool);P(AllocateCommandBuffers);P(BeginCommandBuffer);P(EndCommandBuffer);
 P(CmdPipelineBarrier);P(CmdCopyBufferToImage);P(CmdCopyImageToBuffer);P(GetDeviceQueue);P(QueueSubmit);P(QueueWaitIdle);
 P(MapMemory);P(UnmapMemory);P(InvalidateMappedMemoryRanges);P(DestroyDevice);
 P(CreateSemaphore);P(DestroySemaphore);P(ImportSemaphoreFdKHR);
#undef P
 VkBuffer src=VK_NULL_HANDLE,back=VK_NULL_HANDLE;VkImage dst=VK_NULL_HANDLE;
 VkDeviceMemory srcMem=VK_NULL_HANDLE,dstMem=VK_NULL_HANDLE,backMem=VK_NULL_HANDLE;
 VkCommandPool cp=VK_NULL_HANDLE;
 VkSemaphore s16_wait_sem=VK_NULL_HANDLE;
 VkPhysicalDeviceMemoryProperties props;GetPhysicalDeviceMemoryProperties_fn(phy,&props);
 VkDeviceSize payload=(VkDeviceSize)h->stride*(h->height-1u)+(VkDeviceSize)h->width*4u;
 VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
 VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=payload,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 r=CreateBuffer_fn(dev,&bc,NULL,&src);if(r)goto finish;
 VkMemoryRequirements sreq={0};GetBufferMemoryRequirements_fn(dev,src,&sreq);
 VkMemoryDedicatedRequirements ded={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
 VkMemoryRequirements2 requ2={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&ded};
 VkBufferMemoryRequirementsInfo2 reqinfo={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=src};
 GetBufferMemoryRequirements2_fn(dev,&reqinfo,&requ2);
 printf("S16_COPY_IMPORTED_BUFFER_DEDICATED=%d size=%llu fdsize=%llu\n",ded.requiresDedicatedAllocation,(unsigned long long)sreq.size,(unsigned long long)h->allocation_size);
 if(ded.requiresDedicatedAllocation||sreq.size>h->allocation_size)goto finish;
 VkMemoryFdPropertiesKHR fdprop={.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
 r=GetMemoryFdPropertiesKHR_fn(dev,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,fd,&fdprop);
 if(r)goto finish;
 uint32_t mask=fdprop.memoryTypeBits&sreq.memoryTypeBits;
 int mt=choose_memory(&props,mask,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
 if(mt<0)mt=choose_memory(&props,mask,0);
 if(mt<0)goto finish;
 int copyfd=dup(fd);if(copyfd<0)goto finish;
 VkImportMemoryFdInfoKHR info={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,.fd=copyfd};
 VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&info,.allocationSize=sreq.size,.memoryTypeIndex=(uint32_t)mt};
 r=AllocateMemory_fn(dev,&mai,NULL,&srcMem);printf("S16_COPY_SOURCE_BUFFER_IMPORT_RC=%d\n",r);
 if(r){close(copyfd);goto finish;}
 r=BindBufferMemory_fn(dev,src,srcMem,0);if(r)goto finish;
 VkImageCreateInfo ic={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=(VkFormat)h->format,.extent={h->width,h->height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 r=CreateImage_fn(dev,&ic,NULL,&dst);if(r)goto finish;
 VkMemoryRequirements dreq={0};GetImageMemoryRequirements_fn(dev,dst,&dreq);
 mt=choose_memory(&props,dreq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
 if(mt<0)mt=choose_memory(&props,dreq.memoryTypeBits,0);
 if(mt<0)goto finish;
 mai=(VkMemoryAllocateInfo){.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=dreq.size,.memoryTypeIndex=(uint32_t)mt};
 r=AllocateMemory_fn(dev,&mai,NULL,&dstMem);if(r)goto finish;
 r=BindImageMemory_fn(dev,dst,dstMem,0);if(r)goto finish;
 VkBufferCreateInfo rb={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=(VkDeviceSize)h->width*h->height*4u,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 r=CreateBuffer_fn(dev,&rb,NULL,&back);if(r)goto finish;
 VkMemoryRequirements breq={0};GetBufferMemoryRequirements_fn(dev,back,&breq);
 mt=choose_memory(&props,breq.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
 if(mt<0)mt=choose_memory(&props,breq.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
 if(mt<0)goto finish;
 mai=(VkMemoryAllocateInfo){.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=breq.size,.memoryTypeIndex=(uint32_t)mt};
 r=AllocateMemory_fn(dev,&mai,NULL,&backMem);if(r)goto finish;
 r=BindBufferMemory_fn(dev,back,backMem,0);if(r)goto finish;
 VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=q};
 r=CreateCommandPool_fn(dev,&pci,NULL,&cp);if(r)goto finish;
 VkCommandBuffer cmd;VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
 r=AllocateCommandBuffers_fn(dev,&ca,&cmd);if(r)goto finish;
 VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
 r=BeginCommandBuffer_fn(cmd,&begin);if(r)goto finish;
 VkBufferMemoryBarrier acquire={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.buffer=src,.offset=0,.size=VK_WHOLE_SIZE};
 VkImageSubresourceRange range={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1};
 VkImageMemoryBarrier target={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.image=dst,.subresourceRange=range};
 CmdPipelineBarrier_fn(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&acquire,1,&target);
 VkBufferImageCopy region={.bufferRowLength=h->stride/4u,.bufferImageHeight=h->height,.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},.imageExtent={h->width,h->height,1}};
 CmdCopyBufferToImage_fn(cmd,src,dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 target=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.image=dst,.subresourceRange=range};
 CmdPipelineBarrier_fn(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&target);
 region=(VkBufferImageCopy){.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},.imageExtent={h->width,h->height,1}};
 CmdCopyImageToBuffer_fn(cmd,dst,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,back,1,&region);
 VkBufferMemoryBarrier rbDone={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=back,.offset=0,.size=VK_WHOLE_SIZE};
 CmdPipelineBarrier_fn(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&rbDone,0,NULL);
 r=EndCommandBuffer_fn(cmd);if(r)goto finish;
 VkQueue queue;GetDeviceQueue_fn(dev,q,0,&queue);
 VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
 if(use_sync_file){
  struct dma_buf_export_sync_file snapshot={.flags=DMA_BUF_SYNC_READ,.fd=-1};
  errno=0;
  int er=ioctl(fd,DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&snapshot);
  printf("S16_DMA_BUF_EXPORT_SYNC_FILE=rc:%d errno:%d fd:%d\n",er,errno,snapshot.fd);
  if(er!=0 || snapshot.fd<0)goto finish;
  VkSemaphoreCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  r=CreateSemaphore_fn(dev,&sci,NULL,&s16_wait_sem);
  printf("S16_GPU_WAIT_CREATE_SEMAPHORE=%d\n",r);
  if(r!=VK_SUCCESS){close(snapshot.fd);goto finish;}
  VkImportSemaphoreFdInfoKHR import={.sType=VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
   .semaphore=s16_wait_sem,.flags=VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
   .handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,.fd=snapshot.fd};
  r=ImportSemaphoreFdKHR_fn(dev,&import);
  printf("S16_GPU_WAIT_IMPORT_SYNC_FD=%d\n",r);
  if(r!=VK_SUCCESS){close(snapshot.fd);goto finish;}
 }
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,
  .pCommandBuffers=&cmd,
  .waitSemaphoreCount=use_sync_file?1u:0u,
  .pWaitSemaphores=use_sync_file?&s16_wait_sem:NULL,
  .pWaitDstStageMask=use_sync_file?&wait_stage:NULL};
 r=QueueSubmit_fn(queue,1,&submit,VK_NULL_HANDLE);printf("S16_COPY_BUFFER_TO_IMAGE_SUBMIT=%d\n",r);if(r)goto finish;
 r=QueueWaitIdle_fn(queue);if(r)goto finish;
 void *mapped=NULL;r=MapMemory_fn(dev,backMem,0,VK_WHOLE_SIZE,0,&mapped);if(r)goto finish;
 VkMappedMemoryRange mr={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=backMem,.offset=0,.size=VK_WHOLE_SIZE};
 InvalidateMappedMemoryRanges_fn(dev,1,&mr);
 if(getenv("S16_GRADIENT_TEST")){
  unsigned long long bad0=0,bad1=0,checked=0;
  const unsigned char *pixel=(const unsigned char*)mapped;
  for(unsigned yy=0;yy<h->height;yy++)for(unsigned xx=0;xx<h->width;xx++){
   const unsigned char *p=pixel+((size_t)yy*h->width+xx)*4;
   int r0=xx%251,g0=yy%241,b0=(3*xx+5*yy)%251;
   unsigned y1=h->height-1-yy;
   int r1=r0,g1=y1%241,b1=(3*xx+5*y1)%251;
   int d0=abs((int)p[0]-r0)+abs((int)p[1]-g0)+abs((int)p[2]-b0)+abs((int)p[3]-255);
   int d1=abs((int)p[0]-r1)+abs((int)p[1]-g1)+abs((int)p[2]-b1)+abs((int)p[3]-255);
   if(d0>5)bad0++;
   if(d1>5)bad1++;
   checked++;
  }
  printf("S16_SPATIAL_GRADIENT_GPU_COPY=checked:%llu top_mismatch:%llu bottom_mismatch:%llu\n",checked,bad0,bad1);
  result=(bad0==0)?0:-1; /* Orientation is intentionally not silently flipped. */
  UnmapMemory_fn(dev,backMem);
  goto finish;
 }
 int matched=0;unsigned x[5]={8u,h->width/2u,h->width-8u,h->width/3u,h->width/2u};unsigned y[5]={8u,h->height/2u,h->height-8u,h->height/3u,h->height-8u};
 for(int i=0;i<5;i++){
  const unsigned char *pixel=(const unsigned char*)mapped+((size_t)y[i]*h->width+x[i])*4u;
  int ok=pixel[0]>=24&&pixel[0]<=28&&pixel[1]>=176&&pixel[1]<=182&&pixel[2]>=228&&pixel[2]<=232&&pixel[3]==255;
  matched+=ok;
  printf("S16_COPY_STAGING_PIXEL_%d=%u,%u,%u,%u match=%d\n",i,pixel[0],pixel[1],pixel[2],pixel[3],ok);
 }
 if(getenv("S16_FULL_FRAME_VALIDATE")){
  const unsigned char *pixels=(const unsigned char*)mapped;
  unsigned long long checked=0,mismatches=0;
  for(uint32_t y=0;y<h->height;y++)for(uint32_t x=0;x<h->width;x++){
   const unsigned char*p=pixels+((size_t)y*h->width+x)*4u;
   checked++;
   if(!(p[0]>=24 && p[0]<=28 && p[1]>=176 && p[1]<=182 &&
        p[2]>=228 && p[2]<=232 && p[3]==255))mismatches++;
  }
  printf("S16_COPY_FULL_FRAME_PIXELS=checked:%llu mismatched:%llu\n",
         checked,mismatches);
  if(mismatches)matched=0;
 }
 UnmapMemory_fn(dev,backMem);
 printf("S16_COPY_VKBUFFER_TO_VKIMAGE_GPU_PIXELS=%d/5\n",matched);
 result=(matched==5)?0:-1;
finish:
 if(s16_wait_sem)DestroySemaphore_fn(dev,s16_wait_sem,NULL);
 if(cp)DestroyCommandPool_fn(dev,cp,NULL);
 if(backMem)FreeMemory_fn(dev,backMem,NULL);
 if(back)DestroyBuffer_fn(dev,back,NULL);
 if(dstMem)FreeMemory_fn(dev,dstMem,NULL);
 if(dst)DestroyImage_fn(dev,dst,NULL);
 if(srcMem)FreeMemory_fn(dev,srcMem,NULL);
 if(src)DestroyBuffer_fn(dev,src,NULL);
 DestroyDevice_fn(dev,NULL);
cleanup_instance:
 DestroyInstance_fn(ins,NULL);dlclose(lib);
 return result;
}
