/* Run production Vulkan barrier/pass code through the pinned OpenGNM archive.
 * This proves emitted synchronization, not console pixels or timing. */
#ifndef BACKEND_COMMAND_SOURCE
#define BACKEND_COMMAND_SOURCE "../../ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_command.c"
#endif
#include BACKEND_COMMAND_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern unsigned gnm_diagnostic_count;
static unsigned count_packets(const uint32_t *p, const uint32_t *end,
                              unsigned opcode, uint32_t *coher) {
    unsigned found = 0;
    while (p < end) {
        assert((p[0] >> 30) == 3);
        unsigned words = ((p[0] >> 16) & 0x3fff) + 2;
        assert(p + words <= end);
        if (((p[0] >> 8) & 0xff) == opcode) {
            ++found;
            if (coher) *coher = p[1];
            if (opcode == PKT3_EVENT_WRITE_EOP) {
                assert((p[1] & 0x3f) == GNM_CACHE_FLUSH_AND_INV_TS_EVENT ||
                       (p[1] & 0x3f) == GNM_FLUSH_AND_INV_CB_DATA_TS);
                assert(((uint64_t)(p[3] & 0xffff) << 32 | p[2]) != 0);
                assert((p[3] >> 29) == GNM_DATA_SEL_DISCARD);
            }
        }
        p += words;
    }
    assert(p == end);
    return found;
}
int main(void) {
    VkPs4CommandBuffer *cmd = calloc(1, sizeof(*cmd));
    assert(cmd);
    _Alignas(256) uint32_t pm4[1024];
    cmd->gnm_cmd.beginptr = cmd->gnm_cmd.cmdptr = pm4;
    cmd->gnm_cmd.endptr = pm4 + 1024;
    VkPs4Image image = {0};
    VkPs4ImageView view = {0}; view.image = &image;
    VkPs4ImageView *views[] = {&view};
    VkPs4Framebuffer fb = {0}; fb.attachment_count = 1; fb.attachments = views;
    VkAttachmentDescription attachment = {0};
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkSubpassDependency dep = {0};
    dep.srcSubpass = 0; dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkPs4RenderPass rp = {0};
    rp.attachment_count = 1; rp.attachments = &attachment;
    rp.subpass_dependency_count = 1; rp.dependencies = &dep;
    const uint32_t caches = S_0301F0_TC_ACTION_ENA(1) |
        S_0301F0_TCL1_ACTION_ENA(1) | S_0301F0_SH_KCACHE_ACTION_ENA(1);
    for (unsigned test = 0; test < 5; ++test) {
        cmd->gnm_cmd.cmdptr = pm4;
        cmd->graphics_sync_endptr = NULL;
        cmd->current_render_pass.pass = &rp;
        cmd->current_render_pass.framebuffer = &fb;
        image.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        rp.subpass_dependency_count = test == 2 ? 0 : 1;
        dep.dstAccessMask = test == 1 ? 0 : VK_ACCESS_SHADER_READ_BIT;
        dep.srcSubpass = test == 3 ? VK_SUBPASS_EXTERNAL : 0;
        fb.imageless = test == 4;
        cmd->current_render_pass.imageless_attachment_count = 1;
        cmd->current_render_pass.imageless_attachments[0] = &view;
        vk_ps4_CmdEndRenderPass((VkCommandBuffer)cmd);
        assert(count_packets(pm4, cmd->gnm_cmd.cmdptr, PKT3_EVENT_WRITE_EOP, NULL) == 2);
        uint32_t coher = 0;
        unsigned acquires = count_packets(pm4, cmd->gnm_cmd.cmdptr, PKT3_ACQUIRE_MEM, &coher);
        assert(acquires == (test != 2 && test != 3));
        assert((coher & caches) == (test == 0 || test == 4 ? caches : 0));
        assert(image.layout == attachment.finalLayout);
        assert(!cmd->current_render_pass.pass && !cmd->current_render_pass.framebuffer);
        assert(gnm_diagnostic_count == 0);
        printf("PASS actual EndRenderPass dependency/final-layout case %u acquire=%u coher=0x%08x\n",test,acquires,coher);
        uint32_t *after_pass = cmd->gnm_cmd.cmdptr;
        VkMemoryBarrier barrier = {0};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vk_ps4_CmdPipelineBarrier((VkCommandBuffer)cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        const unsigned expected = test == 0 || test == 4 ? 0 : 1;
        assert(count_packets(after_pass, cmd->gnm_cmd.cmdptr, PKT3_EVENT_WRITE_EOP, NULL) == expected);
        assert(count_packets(after_pass, cmd->gnm_cmd.cmdptr, PKT3_ACQUIRE_MEM, NULL) == expected);
        // After that full shader acquire, another adjacent graphics dependency is covered.
        uint32_t *covered = cmd->gnm_cmd.cmdptr;
        vk_ps4_CmdPipelineBarrier((VkCommandBuffer)cmd,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        assert(cmd->gnm_cmd.cmdptr == covered);
        // An intervening packet invalidates the proof, even a harmless state packet.
        vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_028408_VGT_INDX_OFFSET, 0);
        uint32_t *after_packet = cmd->gnm_cmd.cmdptr;
        vk_ps4_CmdPipelineBarrier((VkCommandBuffer)cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        assert(count_packets(after_packet, cmd->gnm_cmd.cmdptr, PKT3_EVENT_WRITE_EOP, NULL) == 1);
        // CPU-side transfer/host work is not proven absent by a PM4 cursor.
        uint32_t *before_transfer = cmd->gnm_cmd.cmdptr;
        vk_ps4_CmdPipelineBarrier((VkCommandBuffer)cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        assert(count_packets(before_transfer, cmd->gnm_cmd.cmdptr, PKT3_EVENT_WRITE_EOP, NULL) == 1);
        vk_ps4_reset_user_data_state(cmd);
        assert(cmd->graphics_sync_endptr == NULL);

    }
    puts("PASS adjacent full barriers coalesce; stronger reads, intervening packets, transfer scopes and reset preserve synchronization");
    free(cmd);
    puts("PASS actual pinned OpenGNM packet emission; no GPU image or FPS claim");
}
