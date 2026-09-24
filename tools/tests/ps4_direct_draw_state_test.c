/* Production command path, mocked final native driver. No GPU claim. */
#include "../../ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_command.c"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
static unsigned logs, native_draws;
void vk_ps4_flush_descriptor_tables(VkPs4CommandBuffer *c, VkPipelineBindPoint b) { (void)b; assert(!c->graphics_tables_dirty); }
void vk_ps4_log(const char *fmt, ...) { if (strstr(fmt,"[DEPTH_DRAW]")) ++logs; }
/* Only the OS driver boundary is mocked; actual OpenGNM emits other state. */
int sceGnmDriverDrawIndex(uint32_t *p,uint32_t count,uint32_t n,const void *a,SceGnmDrawFlags flags) {
    (void)n;(void)a;(void)flags; assert(count==10); memset(p,0,count*4); ++native_draws; return 0;
}
int main(void) {
    VkPs4CommandBuffer *c=calloc(1,sizeof(*c)); assert(c);
    uint32_t words[4096]={0}, indices[3]={0,1,2};
    c->gnm_cmd.beginptr=c->gnm_cmd.cmdptr=words; c->gnm_cmd.endptr=words+4096;
    VkAttachmentReference dep={.attachment=0};
    VkSubpassDescription sub={.pDepthStencilAttachment=&dep};
    VkPs4RenderPass rp={.subpass_count=1,.subpasses=&sub};
    c->current_render_pass.pass=&rp;
    VkPs4Pipeline pipe={0}; VkPs4Buffer ib={0}; VkPs4DeviceMemory mem={0};
    mem.gnm_mem.mapped=indices; ib.memory=&mem; ib.create_info.size=sizeof(indices);
    c->index_buffer.buffer=(VkBuffer)&ib; c->index_buffer.type=VK_INDEX_TYPE_UINT32;
    vk_ps4_depth_draw_receipt_begin(c); assert(c->depth_draw_diagnostics.active);
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0); assert(c->depth_draw_diagnostics.no_pipeline==1);
    c->current_pipeline=&pipe; pipe.rasterization_state.rasterizerDiscardEnable=1;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0); assert(c->depth_draw_diagnostics.raster_discard==1);
    pipe.rasterization_state.rasterizerDiscardEnable=0; c->recording_error=VK_ERROR_UNKNOWN;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0); assert(c->depth_draw_diagnostics.recording_failed==1);
    c->recording_error=VK_SUCCESS; pipe.vertex_input_state.vertexAttributeDescriptionCount=1;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0); assert(c->depth_draw_diagnostics.no_fetch==1);
    pipe.has_fetch_shader=true; pipe.vs_input_semantic_count=3;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0); assert(c->depth_draw_diagnostics.vertex_table==1);
    pipe.vertex_input_state.vertexAttributeDescriptionCount=0;
    c->index_buffer.buffer=VK_NULL_HANDLE;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0); assert(c->depth_draw_diagnostics.index_buffer==1);
    c->index_buffer.buffer=(VkBuffer)&ib;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,4,1,0,0,0); assert(c->depth_draw_diagnostics.index_range==1);
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0);
    assert(native_draws==1 && c->depth_draw_diagnostics.emitted==1 && c->depth_draw_diagnostics.attempts==8);
    vk_ps4_depth_draw_receipt_end(c); assert(logs==1 && !c->depth_draw_diagnostics.active);
    /* Actual production draw path, unchanged draws but fewer native PM4 words. */
    c->direct_draw_state_valid=false;
    uint32_t *start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0);
    size_t first_words=(size_t)(c->gnm_cmd.cmdptr-start);
    start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0);
    size_t repeated_words=(size_t)(c->gnm_cmd.cmdptr-start);
    assert(first_words==repeated_words+5 && repeated_words==10);
    start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,2,0,-3,0);
    assert((size_t)(c->gnm_cmd.cmdptr-start)==first_words);
    assert(c->direct_draw_instances==2 && c->direct_draw_vertex_offset==(uint32_t)-3);

    /* Pipeline-relative base/start user-data: the first draw emits it, an
     * identical draw reuses it, firstInstance or pipeline changes re-emit. */
    pipe.has_base_vertex_reg=true; pipe.has_start_instance_reg=true;
    pipe.vs_base_vertex_reg=5; pipe.vs_start_instance_reg=6;
    c->direct_draw_state_valid=false; c->direct_draw_userdata_valid=false;
    start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0);
    size_t userdata_first=(size_t)(c->gnm_cmd.cmdptr-start);
    start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,0);
    size_t userdata_repeated=(size_t)(c->gnm_cmd.cmdptr-start);
    assert(userdata_first==userdata_repeated+9 && userdata_repeated==10);
    start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,7);
    assert((size_t)(c->gnm_cmd.cmdptr-start)==userdata_repeated+4);
    VkPs4Pipeline pipe2=pipe; c->current_pipeline=&pipe2;
    start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,1,0,0,7);
    assert((size_t)(c->gnm_cmd.cmdptr-start)==userdata_repeated+4);
    c->current_pipeline=&pipe;
    assert(c->recording_perf.direct_userdata_reuses>=1);
    assert(c->recording_perf.direct_userdata_writes>=3);

    vk_ps4_reset_user_data_state(c); assert(!c->direct_draw_state_valid && !c->direct_draw_userdata_valid);
    start=c->gnm_cmd.cmdptr;
    vk_ps4_CmdDrawIndexed((VkCommandBuffer)c,3,2,0,-3,0);
    assert((size_t)(c->gnm_cmd.cmdptr-start)==userdata_first);
    printf("PASS production indexed state reuse: base first=%zu repeated=%zu; shader-userdata first=%zu repeated=%zu PM4 dwords\n",
           first_words,repeated_words,userdata_first,userdata_repeated);
    c->current_pipeline=NULL;
    vk_ps4_CmdDrawIndirect((VkCommandBuffer)c,(VkBuffer)&ib,0,1,16);
    assert(!c->direct_draw_state_valid && !c->direct_draw_userdata_valid);
    c->direct_draw_state_valid=true; c->direct_draw_userdata_valid=true;
    vk_ps4_CmdDrawIndexedIndirect((VkCommandBuffer)c,(VkBuffer)&ib,0,1,20);
    assert(!c->direct_draw_state_valid && !c->direct_draw_userdata_valid);
    VkPs4CommandBuffer *secondary=calloc(1,sizeof(*secondary)); assert(secondary);
    secondary->level=VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    secondary->gnm_cmd.beginptr=secondary->gnm_cmd.cmdptr=words;
    VkCommandBuffer secondary_handle=(VkCommandBuffer)secondary;
    c->direct_draw_state_valid=true; c->direct_draw_userdata_valid=true;
    vk_ps4_CmdExecuteCommands((VkCommandBuffer)c,1,&secondary_handle);
    assert(!c->direct_draw_state_valid && !c->direct_draw_userdata_valid);free(secondary);
    puts("PASS indirect and secondary execution invalidate direct draw register shadow");
    for(unsigned i=2;i<=601;++i) {vk_ps4_depth_draw_receipt_begin(c);vk_ps4_depth_draw_receipt_end(c);}
    assert(logs==5); /* first 3, 300 and 600 per command buffer */
    sub.colorAttachmentCount=1; vk_ps4_depth_draw_receipt_begin(c); assert(!c->depth_draw_diagnostics.active);
    free(c); puts("PASS production indexed command rejection paths, native emitter receipt, sparse depth-only logging; OS draw boundary mocked");
}
