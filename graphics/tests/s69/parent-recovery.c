/* CPU-only. Use the production recovery state machine and Vulkan timing filter,
 * supplying controlled monotonic times and driver presentation records. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

static uint64_t clock_ns;
static VkDevice g_dev;
static VkSwapchainKHR g_swapchain;
static VkPastPresentationTimingGOOGLE record;
static unsigned queries;

static int test_clock(clockid_t ignored, struct timespec *t) {
    (void)ignored;
    t->tv_sec=clock_ns/1000000000;
    t->tv_nsec=clock_ns%1000000000;
    return 0;
}
#define clock_gettime test_clock
#include "../../../native/waylandcomp/src/s69_parent_timing.inc"
#undef clock_gettime
#include "../../../native/waylandcomp/src/s69_parent_recovery.inc"

static VKAPI_ATTR VkResult VKAPI_CALL driver_history(VkDevice dev,VkSwapchainKHR chain,
                                                     uint32_t *n,VkPastPresentationTimingGOOGLE *v){
    (void)dev;
    assert(chain==g_swapchain && *n>=1);
    ++queries;
    if(record.presentID){*n=1;v[0]=record;}else *n=0;
    return VK_SUCCESS;
}
static uint32_t queue_parent(void) {
    assert(s69_armed);
    VkPresentInfoKHR pi={0};
    VkPresentTimesInfoGOOGLE times;
    VkPresentTimeGOOGLE when;
    s69_tag_parent(&pi,&times,&when);
    assert(pi.pNext==&times);
    s69_note_queue(VK_SUCCESS,when.presentID);
    return when.presentID;
}
static bool step(int ms,uint64_t epoch,uint64_t serial,bool busy){
    clock_ns=(uint64_t)ms*1000000;
    return s69_recover_parent(epoch,serial,busy,clock_ns);
}
int main(void){
    g_swapchain=(VkSwapchainKHR)(uintptr_t)17;s69_get_past=driver_history;
    s69r.epoch=2;
    s69_recovery_cancel("await_baseline",1000000);
    assert(!step(249,2,1,false) && s69r.attempts==0);
    assert(!step(252,2,2,false) && s69r.attempts==1);
    uint32_t first=queue_parent();
    assert(!step(260,2,3,false) && !queries);
    record.presentID=first+1;record.actualPresentTime=260000000;
    assert(!step(270,2,3,false));                     /* wrong tagged ID */
    record.presentID=first;record.actualPresentTime=251000000;
    assert(!step(290,2,3,false));                     /* stale timestamp */
    record.actualPresentTime=1000000000;
    assert(!step(310,2,3,false));                     /* future timestamp */
    assert(!step(752,2,3,false) && s69r.failures==1 && !s69r.watching);
    assert(!step(900,2,3,false) && s69r.attempts==1); /* bounded retry */

    assert(!step(910,3,4,false) && s69r.attempts==2); /* resumed parent epoch */
    uint32_t second=queue_parent();
    record.presentID=first;record.actualPresentTime=911000000;
    assert(!step(930,3,5,false));                      /* previous watch/epoch */
    record.presentID=second;
    assert(!step(950,3,4,false) && s69r.proved==1);    /* same use as tagged parent */
    assert(!step(970,3,5,true));                      /* outstanding image ownership */
    g_swapchain=(VkSwapchainKHR)(uintptr_t)18;
    assert(!step(980,3,5,false) && !s69r.restored);    /* chain changed after proof */

    assert(!step(1000,4,6,false));
    uint32_t third=queue_parent();
    record.presentID=third;record.actualPresentTime=1003000000;
    assert(step(1020,4,7,false) && s69r.restored==1 && !s69r.watching);
    assert(!s69_armed && s69_timing_chain==VK_NULL_HANDLE && s69r.proved==2);

    assert(!step(1030,5,8,false));                    /* skipped first baseline draw */
    assert(!step(1036,5,9,false) && !s69_armed);       /* cannot tag another source */
    assert(!step(1060,5,9,false) && s69r.restored==1);
    s69_recovery_cancel("root_changed",clock_ns);
    assert(!s69r.watching && !s69r.proven && !s69_armed);
    puts("PASS recovery requires newer tagged actual parent timing, current epoch/chain and fresh idle source");
}
