/* Wayland-thread only. The regular display path never requests timing. On a
 * direct-layer handoff, keep that layer until a tagged normal-parent frame
 * actually presented on THIS swapchain. A signaled render fence or a return
 * from QueuePresent cannot prove this. Never wait on an unsignaled fence. */
static PFN_vkGetPastPresentationTimingGOOGLE s69_get_past;
static VkSwapchainKHR s69_timing_chain;
static uint32_t s69_sequence,s69_first_id,s69_last_id;
static uint64_t s69_watch_start_ns;
static int s69_armed,s69_timing_broken;
static uint64_t s69_timing_now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
void vkp_s69_cancel_present(void) {
    s69_timing_chain=VK_NULL_HANDLE;s69_armed=s69_timing_broken=0;
    s69_first_id=s69_last_id=0;s69_watch_start_ns=0;
}
int vkp_s69_can_watch_present(void) {
    return s69_get_past && g_swapchain && s69_sequence<UINT32_MAX;
}
int vkp_s69_watch_present(void) {
    if(!vkp_s69_can_watch_present() || s69_timing_broken)return 0;
    if(s69_timing_chain && s69_timing_chain!=g_swapchain)return 0;
    if(!s69_timing_chain) {
        s69_timing_chain=g_swapchain;s69_watch_start_ns=s69_timing_now();
        if(!s69_watch_start_ns){vkp_s69_cancel_present();return 0;}
    }
    s69_armed=1;return 1;
}
int vkp_s69_parent_latched(void) {
    if(!s69_timing_chain || g_swapchain!=s69_timing_chain || !s69_get_past || s69_timing_broken)return -1;
    if(!s69_first_id)return 0;
    VkPastPresentationTimingGOOGLE v[32];uint32_t n=32;
    VkResult res=s69_get_past(g_dev,s69_timing_chain,&n,v);
    if(res!=VK_SUCCESS && res!=VK_INCOMPLETE){s69_timing_broken=1;return -1;}
    const uint64_t now=s69_timing_now();
    for(uint32_t i=0;i<n&&i<32;i++){
        /* No other subsystem emits GOOGLE IDs. Only successful, armed
         * mixed-parent submissions own IDs in this strictly monotonic range. */
        if(v[i].presentID<s69_first_id || v[i].presentID>s69_last_id ||
           !now || v[i].actualPresentTime<s69_watch_start_ns || v[i].actualPresentTime>now)continue;
        return 1;
    }
    return 0;
}
static void s69_tag_parent(VkPresentInfoKHR *pi,VkPresentTimesInfoGOOGLE *times,VkPresentTimeGOOGLE *when) {
    s69_armed=0;
    if(!s69_get_past || s69_timing_chain!=g_swapchain || s69_sequence==UINT32_MAX){s69_timing_broken=1;return;}
    when->presentID=++s69_sequence;when->desiredPresentTime=0;
    *times=(VkPresentTimesInfoGOOGLE){.sType=VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,.pNext=pi->pNext,.swapchainCount=1,.pTimes=when};
    pi->pNext=times;
}
static void s69_note_queue(VkResult r,uint32_t id) {
    if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR){s69_timing_broken=1;return;}
    if(!s69_first_id)s69_first_id=id;
    s69_last_id=id;
}
