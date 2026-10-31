#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile extracted production submit functions against a recording vendor mock."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path, nargs='?', default=Path(__file__).resolve().parents[2] / '.source/gpu-stage1/s44-sync-overhead/wrapper/source/src/vulkan/wrapper/wrapper_device.c')
args = parser.parse_args()
source = args.source.read_text()
a = source.index('#define S44_SUBMIT_STACK_BATCHES')
b = source.index('const struct vk_device_extension_table', a)
globals_and_helpers = source[a:b]
a = source.index('static struct wrapper_fence *\nget_wrapper_fence_from_handle')
b = source.index('static void\nwrapper_filter_enabled_extensions', a)
fence_lookup = source[a:b]
a = source.index('VKAPI_ATTR VkResult VKAPI_CALL\nwrapper_QueueSubmit(')
b = source.index('static void\nwrapper_fence_destroy', a)
submits = source[a:b]
prelude = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
typedef void *VkQueue;
typedef void *VkCommandBuffer;
typedef uint64_t VkFence;
typedef uint64_t VkSemaphore;
typedef int32_t VkResult;
typedef uint32_t VkPipelineStageFlags;
#define VK_SUCCESS 0
#define VK_ERROR_DEVICE_LOST -4
#define VK_NULL_HANDLE 0
#define VKAPI_ATTR
#define VKAPI_CALL
#define VK_FROM_HANDLE(type,name,value) struct type *name=(struct type *)(value)
typedef struct VkSubmitInfo {
 uint32_t sType; const void *pNext; uint32_t waitSemaphoreCount;
 const VkSemaphore *pWaitSemaphores; const VkPipelineStageFlags *pWaitDstStageMask;
 uint32_t commandBufferCount; const VkCommandBuffer *pCommandBuffers;
 uint32_t signalSemaphoreCount; const VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;
typedef struct VkSemaphoreSubmitInfo {
 uint32_t sType; const void *pNext; VkSemaphore semaphore;
 uint64_t value,stageMask; uint32_t deviceIndex;
} VkSemaphoreSubmitInfo;
typedef struct VkCommandBufferSubmitInfo {
 uint32_t sType; const void *pNext; VkCommandBuffer commandBuffer; uint32_t deviceMask;
} VkCommandBufferSubmitInfo;
typedef struct VkSubmitInfo2 {
 uint32_t sType; const void *pNext; uint32_t flags,waitSemaphoreInfoCount;
 const VkSemaphoreSubmitInfo *pWaitSemaphoreInfos; uint32_t commandBufferInfoCount;
 const VkCommandBufferSubmitInfo *pCommandBufferInfos; uint32_t signalSemaphoreInfoCount;
 const VkSemaphoreSubmitInfo *pSignalSemaphoreInfos;
} VkSubmitInfo2;
struct wrapper_fence { uint64_t marker; } valid_wf={0x1234};
struct wrapper_command_buffer { VkCommandBuffer dispatch_handle; struct wrapper_fence *fence; };
struct wrapper_device {
 int resource_mutex; void *fence_table;
 struct {
  VkResult (*QueueSubmit)(VkQueue,uint32_t,const VkSubmitInfo *,VkFence);
  VkResult (*QueueSubmit2)(VkQueue,uint32_t,const VkSubmitInfo2 *,VkFence);
 } dispatch_table;
};
struct wrapper_queue { struct wrapper_device *device; VkQueue dispatch_handle; };
static unsigned lock_calls,lookup_calls,held,alloc_calls,live_allocs;
static void *allocated[64];
static void simple_mtx_lock(int *mutex) { (void)mutex; assert(!held);held=1;++lock_calls; }
static void simple_mtx_unlock(int *mutex) { (void)mutex;assert(held);held=0; }
static void *_mesa_hash_table_u64_search(void *table,uint64_t handle) {
 assert(table==(void *)&valid_wf && held);++lookup_calls;
 return handle==valid_wf.marker ? &valid_wf : NULL;
}
static void *tracked_malloc(size_t size) {
 assert(alloc_calls<64);void *p=malloc(size ? size : 1);assert(p);
 allocated[alloc_calls++]=p;++live_allocs;return p;
}
static void tracked_free(void *p) {
 if(!p)return;
 unsigned i;for(i=0;i<alloc_calls;i++)if(allocated[i]==p)break;
 assert(i<alloc_calls);allocated[i]=NULL;assert(live_allocs);--live_allocs;free(p);
}
#define malloc tracked_malloc
#define free tracked_free
'''
tests = r'''
#undef malloc
#undef free
static VkSubmitInfo input1[6],saved1[6];
static VkSubmitInfo2 input2[6],saved2[6];
static VkCommandBuffer handles[6][16];
static VkCommandBufferSubmitInfo cbinfo[6][16],saved_cbinfo[6][16];
static VkSemaphore semaphores[6][4];
static VkPipelineStageFlags stages[6][4];
static VkSemaphoreSubmitInfo seminfo[6][4];
/* Opaque pNext payloads include a timeline value array and a linked extension. */
static struct next_node { uint32_t type; const void *next; uint64_t values[4]; } ext[6],tail[6],saved_ext[6];
static struct wrapper_command_buffer commands[32];
static int native_commands[32],native_queue;
static unsigned expected_count,vendor_calls;
static VkFence expected_fence;
static VkResult vendor_result;
static bool enabled,expected_fast;
static void check_begin(VkQueue queue,uint32_t count,VkFence fence) {
 assert(queue==&native_queue && count==expected_count && fence==expected_fence);
 ++vendor_calls;assert(vendor_calls==1);
 assert(live_allocs==(expected_fast ? 0 : count));
}
static void check_command(VkCommandBuffer wrapped,VkCommandBuffer native) {
 struct wrapper_command_buffer *w=(struct wrapper_command_buffer *)wrapped;
 assert(native==w->dispatch_handle);
 assert(w->fence==(expected_fence ? &valid_wf : NULL));
}
static VkResult vendor1(VkQueue queue,uint32_t count,const VkSubmitInfo *info,VkFence fence) {
 check_begin(queue,count,fence);if(!count)assert(info==NULL);
 for(unsigned i=0;i<count;i++) {
  assert(info[i].sType==input1[i].sType && info[i].pNext==input1[i].pNext);
  assert(info[i].waitSemaphoreCount==input1[i].waitSemaphoreCount);
  assert(info[i].pWaitSemaphores==input1[i].pWaitSemaphores);
  assert(info[i].pWaitDstStageMask==input1[i].pWaitDstStageMask);
  assert(info[i].signalSemaphoreCount==input1[i].signalSemaphoreCount);
  assert(info[i].pSignalSemaphores==input1[i].pSignalSemaphores);
  assert(info[i].commandBufferCount==input1[i].commandBufferCount);
  if(expected_fast && !info[i].commandBufferCount)assert(info[i].pCommandBuffers==input1[i].pCommandBuffers);
  for(unsigned j=0;j<info[i].commandBufferCount;j++)check_command(input1[i].pCommandBuffers[j],info[i].pCommandBuffers[j]);
 }
 return vendor_result;
}
static VkResult vendor2(VkQueue queue,uint32_t count,const VkSubmitInfo2 *info,VkFence fence) {
 check_begin(queue,count,fence);if(!count)assert(info==NULL);
 for(unsigned i=0;i<count;i++) {
  assert(info[i].sType==input2[i].sType && info[i].pNext==input2[i].pNext && info[i].flags==input2[i].flags);
  assert(info[i].waitSemaphoreInfoCount==input2[i].waitSemaphoreInfoCount);
  assert(info[i].pWaitSemaphoreInfos==input2[i].pWaitSemaphoreInfos);
  assert(info[i].signalSemaphoreInfoCount==input2[i].signalSemaphoreInfoCount);
  assert(info[i].pSignalSemaphoreInfos==input2[i].pSignalSemaphoreInfos);
  assert(info[i].commandBufferInfoCount==input2[i].commandBufferInfoCount);
  if(expected_fast && !info[i].commandBufferInfoCount)assert(info[i].pCommandBufferInfos==input2[i].pCommandBufferInfos);
  for(unsigned j=0;j<info[i].commandBufferInfoCount;j++) {
   const VkCommandBufferSubmitInfo *out=&info[i].pCommandBufferInfos[j],*in=&input2[i].pCommandBufferInfos[j];
   assert(out->sType==in->sType && out->pNext==in->pNext && out->deviceMask==in->deviceMask);
   check_command(in->commandBuffer,out->commandBuffer);
  }
 }
 return vendor_result;
}
static void run(unsigned api,unsigned batches,const unsigned *counts,VkFence fence,VkResult result) {
 memset(input1,0,sizeof(input1));memset(input2,0,sizeof(input2));memset(cbinfo,0,sizeof(cbinfo));
 unsigned total=0;
 for(unsigned i=0;i<batches;i++) {
  ext[i]=(struct next_node){101+i,&tail[i],{UINT64_MAX-i,23+i,37+i,49+i}};
  for(unsigned j=0;j<counts[i];j++) {
   assert(total<32);commands[total]=(struct wrapper_command_buffer){&native_commands[total],NULL};
   handles[i][j]=&commands[total++];
   cbinfo[i][j]=(VkCommandBufferSubmitInfo){301+i,&tail[i],handles[i][j],1u<<j};
  }
  input1[i]=(VkSubmitInfo){11+i,&ext[i],2,semaphores[i],stages[i],counts[i],counts[i]?handles[i]:NULL,2,semaphores[i]+2};
  input2[i]=(VkSubmitInfo2){21+i,&ext[i],7+i,2,seminfo[i],counts[i],counts[i]?cbinfo[i]:NULL,2,seminfo[i]+2};
 }
 memcpy(saved1,input1,sizeof(input1));memcpy(saved2,input2,sizeof(input2));
 memcpy(saved_cbinfo,cbinfo,sizeof(cbinfo));memcpy(saved_ext,ext,sizeof(ext));
 expected_count=batches;expected_fence=fence;vendor_result=result;
 expected_fast=enabled && batches>0 && batches<=5 && total<=8;
 lock_calls=lookup_calls=held=alloc_calls=live_allocs=vendor_calls=0;
 memset(allocated,0,sizeof(allocated));
 memset(s44_submit_calls,0,sizeof(s44_submit_calls));memset(s44_submit_stack_calls,0,sizeof(s44_submit_stack_calls));
 memset(s44_submit_stack_batches,0,sizeof(s44_submit_stack_batches));
 s44_submit_heap_calls=s44_submit_commands=s44_submit_null_fence_skips=0;
 struct wrapper_device device={0,&valid_wf,{vendor1,vendor2}};
 struct wrapper_queue queue={&device,&native_queue};
 VkResult actual=api==1 ? wrapper_QueueSubmit(&queue,batches,batches?input1:NULL,fence) :
                         wrapper_QueueSubmit2(&queue,batches,batches?input2:NULL,fence);
 assert(actual==result && vendor_calls==1 && !live_allocs && !held);
 assert(alloc_calls==(expected_fast ? 0 : batches));
 assert(lock_calls==((batches && (!enabled || fence))?1u:0u) && lookup_calls==lock_calls);
 assert(s44_submit_calls[api-1]==1 && s44_submit_stack_calls[api-1]==(unsigned)expected_fast);
 assert(s44_submit_stack_batches[api-1]==(expected_fast?batches:0) && s44_submit_commands==total);
 assert(s44_submit_heap_calls==alloc_calls);
 assert(!memcmp(saved1,input1,sizeof(input1)) && !memcmp(saved2,input2,sizeof(input2)));
 assert(!memcmp(saved_cbinfo,cbinfo,sizeof(cbinfo)) && !memcmp(saved_ext,ext,sizeof(ext)));
}
int main(int argc,char **argv) {
 assert(argc==2);enabled=strcmp(argv[1],"0")!=0;
 if(!strcmp(argv[1],"default"))assert(!unsetenv("S44_WRAPPER_SUBMIT_STACK"));
 else assert(!setenv("S44_WRAPPER_SUBMIT_STACK",enabled?"1":"0",1));
 assert(!setenv("S44_WRAPPER_PROFILE","1",1));
 assert(s44_submit_stack_enabled()==enabled);
 const unsigned sets[][6]={{2,2},{1,0,2,0},{2,0,3,0,3},{0,0,0,0,0},{9},{1,1,1,1,1,1},{0}};
 const unsigned lengths[]={2,4,5,5,1,6,1};
 for(unsigned api=1;api<=2;api++) {
  run(api,0,NULL,0,VK_SUCCESS);run(api,0,NULL,valid_wf.marker,VK_SUCCESS);
  for(unsigned i=0;i<7;i++)for(unsigned f=0;f<2;f++)run(api,lengths[i],sets[i],f?valid_wf.marker:0,VK_SUCCESS);
  run(api,2,sets[0],valid_wf.marker,VK_ERROR_DEVICE_LOST);
 }
 VkSubmitInfo too_big1[2]={{0}};VkSubmitInfo2 too_big2[2]={{0}};
 too_big1[0].commandBufferCount=UINT32_MAX;too_big1[1].commandBufferCount=9;
 too_big2[0].commandBufferInfoCount=UINT32_MAX;too_big2[1].commandBufferInfoCount=9;
 assert(!s44_submit1_stack_fits(2,too_big1) && !s44_submit2_stack_fits(2,too_big2));
 assert(!s44_submit1_stack_fits(UINT32_MAX,NULL) && !s44_submit2_stack_fits(UINT32_MAX,NULL));
 assert(s44_submit1_stack_fits(0,NULL) && s44_submit2_stack_fits(0,NULL));
 s44_submit_report();
 printf("S44_SUBMIT_MOCK=PASS mode=%s enabled=%u zero/multi/limits/overflow/pNext/CBInfo/lifetime/fence/error\n",argv[1],enabled);
 return 0;
}
'''
compiler = shutil.which('clang') or shutil.which('cc')
if compiler is None:
    raise SystemExit('C compiler unavailable')
with tempfile.TemporaryDirectory(prefix='s44-submit-mock-') as directory:
    c = Path(directory) / 'check.c'; binary = Path(directory) / 'check'
    c.write_text(prelude + globals_and_helpers + fence_lookup + submits + tests)
    subprocess.run([compiler,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-sign-compare',
                    '-fsanitize=address,undefined','-fsanitize-address-use-after-scope','-fno-omit-frame-pointer',
                    '-pthread',str(c),'-o',str(binary)],check=True)
    # LeakSanitizer cannot enumerate /proc tasks in this sandbox. The fixture
    # independently checks allocation ownership and all frees on every call.
    env=dict(os.environ,ASAN_OPTIONS='detect_stack_use_after_return=1:detect_leaks=0')
    for enabled in ('0','1','default'):
        subprocess.run([str(binary),enabled],env=env,check=True)
