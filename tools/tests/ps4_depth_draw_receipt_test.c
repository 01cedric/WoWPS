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
    for(unsigned i=2;i<=601;++i) {vk_ps4_depth_draw_receipt_begin(c);vk_ps4_depth_draw_receipt_end(c);}
    assert(logs==5); /* first 3, 300 and 600 per command buffer */
    sub.colorAttachmentCount=1; vk_ps4_depth_draw_receipt_begin(c); assert(!c->depth_draw_diagnostics.active);
    free(c); puts("PASS production indexed command rejection paths, native emitter receipt, sparse depth-only logging; OS draw boundary mocked");
}
