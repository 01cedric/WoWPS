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
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0, GNM_DATA_SEL_DISCARD, 0);
    assert(cmd->gnm_cmd.cmdptr == pm4 && gnm_diagnostic_count == 1);
    puts("PASS historical NULL/DISCARD EOP emits no packet in actual pinned archive");
    sceGnmDrawCmdWaitGraphicsWrite(&cmd->gnm_cmd, GNM_ACQUIRE_TARGET_DB | GNM_ACQUIRE_TARGET_CB0);
    uint32_t legacy_coher = 0;
    assert(count_packets(pm4, cmd->gnm_cmd.cmdptr, PKT3_ACQUIRE_MEM, &legacy_coher) == 1);
    assert(!G_0301F0_TC_ACTION_ENA(legacy_coher) && !G_0301F0_TCL1_ACTION_ENA(legacy_coher));
    printf("PASS historical acquire lacks full texture-cache invalidate: coher=0x%08x\n", legacy_coher);
    cmd->gnm_cmd.cmdptr = pm4;

    vk_ps4_CmdEndRenderPass((VkCommandBuffer)cmd);
    assert(count_packets(pm4, cmd->gnm_cmd.cmdptr, PKT3_EVENT_WRITE_EOP, NULL) == 2);
    assert(gnm_diagnostic_count == 1);
    puts("PASS production EndRenderPass emits both valid non-writing EOP releases");

    VkPs4Image image = {0};
    image.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkImageMemoryBarrier barrier = {0};
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = image.layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.image = (VkImage)&image;
    const uint32_t shader_caches = S_0301F0_TC_ACTION_ENA(1) |
        S_0301F0_TCL1_ACTION_ENA(1) | S_0301F0_SH_KCACHE_ACTION_ENA(1);
    for (unsigned test = 0; test < 3; ++test) {
        cmd->gnm_cmd.cmdptr = pm4;
        VkMemoryBarrier memory = {0};
        VkBufferMemoryBarrier buffer = {0};
        memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        buffer.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        vk_ps4_CmdPipelineBarrier((VkCommandBuffer)cmd,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            test == 1, test == 1 ? &memory : NULL,
            test == 2, test == 2 ? &buffer : NULL,
            test == 0, test == 0 ? &barrier : NULL);
        uint32_t coher = 0;
        assert(count_packets(pm4, cmd->gnm_cmd.cmdptr, PKT3_EVENT_WRITE_EOP, NULL) == 1);
        assert(count_packets(pm4, cmd->gnm_cmd.cmdptr, PKT3_ACQUIRE_MEM, &coher) == 1);
        assert((coher & shader_caches) == shader_caches);
        assert(G_0301F0_DB_ACTION_ENA(coher) && G_0301F0_CB_ACTION_ENA(coher));
        assert(G_0301F0_DB_DEST_BASE_ENA(coher));
        printf("PASS production image/buffer/memory shader-read barrier %u: coher=0x%08x\n", test, coher);
    }
    assert(image.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cmd->gnm_cmd.cmdptr = pm4;
    vk_ps4_CmdPipelineBarrier((VkCommandBuffer)cmd, 0, 0, 0, 0, NULL, 0, NULL, 0, NULL);
    uint32_t coher = 0;
    assert(count_packets(pm4, cmd->gnm_cmd.cmdptr, PKT3_ACQUIRE_MEM, &coher) == 1);
    assert((coher & shader_caches) == 0); // no extra shader-cache work without a read dependency
    assert(gnm_diagnostic_count == 1);
    free(cmd);
    puts("PASS actual PM4 release/acquire coverage; no GPU shadow-visibility claim");
}
