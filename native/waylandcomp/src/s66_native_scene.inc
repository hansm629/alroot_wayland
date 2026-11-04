/* S63 sampled, SDR, non-nested final-attachment pass ONLY. A pooled/aliased
 * wl_buffer retains all its producer/consumer synchronization even if hidden. */
static uint64_t s66_culled,s66_barriers,s66_commands;
static int s66_native_scene_enabled(void) {
    static int on=-1;
    if(on<0){const char *s=getenv("S66_NATIVE_SCENE");on=!s||!strcmp(s,"1");}
    return on;
}
void vkp_s66_stats(char *buf,size_t len) {
    snprintf(buf,len,"S66_COMPOSE_STATS enabled=%u submitted_hidden_draws_removed=%llu recorded_image_barriers=%llu recorded_barrier_cmds=%llu",
        s66_native_scene_enabled(),(unsigned long long)s66_culled,(unsigned long long)s66_barriers,(unsigned long long)s66_commands);
}
struct s66_box {int l,t,r,b;};
static struct s66_box s66_box_of(const struct blendp_item *v) {
    const VkOffset3D *a=v->rect.dstOffsets;
    return (struct s66_box){a[0].x<a[1].x?a[0].x:a[1].x,a[0].y<a[1].y?a[0].y:a[1].y,
                           a[0].x>a[1].x?a[0].x:a[1].x,a[0].y>a[1].y?a[0].y:a[1].y};
}
static int s66_encloses(struct s66_box outer,struct s66_box inner) {
    return outer.l<=inner.l && outer.t<=inner.t && outer.r>=inner.r && outer.b>=inner.b;
}
static int s66_cull_scene(struct blendp_item *items,int n,int *ahb,int *alpha) {
    /* O(n), bounded 32 items. Single proven solid rectangle only; never
     * treat the bounding box of disjoint windows as actually covered. */
    _Static_assert(BLENDP_MAX_SCENE<=32,"mask width");
    uint32_t hidden=0;
    struct s66_box cover={0};int solid=0;
    for(int i=n-1;i>=0;--i) {
        const struct s66_box box=s66_box_of(&items[i]);
        if(solid && s66_encloses(cover,box)) {
            hidden|=UINT32_C(1)<<i;*ahb-=items[i].ahb;*alpha-=items[i].blend;
        } else if(!items[i].blend && box.l<box.r && box.t<box.b) {
            cover=box;solid=1;
        }
    }
    if(!hidden)return n;
    int kept=0;
    for(int i=0;i<n;++i)if(!(hidden & (UINT32_C(1)<<i))) {
        if(kept!=i)items[kept]=items[i];
        ++kept;
    }
    return kept;
}
static void s66_emit(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,unsigned count) {
    if(!count)return;
    g_vk.CmdPipelineBarrier(cmd,src,dst,0,0,NULL,0,NULL,count,g_scene_barriers);
    if(vkp_s47_profile_enabled()){s66_barriers+=count;++s66_commands;}
}
static void s66_record_native_barriers(VkCommandBuffer cmd,int release) {
    /* The source set already holds one entry per imported image and the
     * caller reserved >= image_count slots. Each barrier in a batch targets
     * a distinct VkImage; retain the original stage/access/layout/ownership
     * pair exactly. A host-write visibility barrier is a DIFFERENT dependency
     * and remains a separate command before drawing. Nothing here waits. */
    const VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    unsigned nb=0;
    for(unsigned i=0;i<g_sources.image_count;++i) {
        struct vkp_image *im=g_sources.images[i];
        if(!im->explicit_sync && !im->s68_legacy_read)continue;
        g_scene_barriers[nb++]=(VkImageMemoryBarrier){
            .sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex=release?g_qfam:VK_QUEUE_FAMILY_FOREIGN_EXT,.dstQueueFamilyIndex=release?VK_QUEUE_FAMILY_FOREIGN_EXT:g_qfam,
            .srcAccessMask=release?(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT):0,
            .dstAccessMask=release?0:(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT),.image=im->image,.subresourceRange=range};
    }
    s66_emit(cmd,release?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 release?VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,nb);
    if(release)return;
    nb=0;
    for(unsigned i=0;i<g_sources.image_count;++i) {
        struct vkp_image *im=g_sources.images[i];
        if(im->dmabuf)continue;
        g_scene_barriers[nb++]=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout=im->in_general?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_PREINITIALIZED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=im->image,.subresourceRange=range,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
        im->in_general=1;
    }
    s66_emit(cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,nb);
}
