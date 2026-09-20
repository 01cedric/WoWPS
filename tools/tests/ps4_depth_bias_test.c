/* Execute the production Vulkan attachment/bias path and the exact bundled
 * OpenGNM PM4 emitters. No driver submission, GPU pixels or timing claims. */
#ifndef BACKEND_COMMAND_SOURCE
#define BACKEND_COMMAND_SOURCE "../../ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_command.c"
#endif
#include BACKEND_COMMAND_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t register_value(const uint32_t *p, const uint32_t *end, uint32_t reg) {
    bool found = false;
    uint32_t value = 0;
    while (p < end) {
        assert((p[0] >> 30) == 3);
        const uint32_t count = ((p[0] >> 16) & 0x3fff) + 2;
        assert(p + count <= end);
        if (((p[0] >> 8) & 0xff) == PKT3_SET_CONTEXT_REG) {
            const uint32_t base = SI_CONTEXT_REG_OFFSET + 4 * (p[1] & 0xffff);
            if (reg >= base && (reg - base) / 4 < count - 2) {
                value = p[2 + (reg - base) / 4]; found = true;
            }
        }
        p += count;
    }
    assert(found && "required context register missing from production PM4");
    return value;
}

int main(void) {
    VkPs4CommandBuffer *cmd = calloc(1, sizeof(*cmd));
    assert(cmd);
    uint32_t pm4[1024];
    cmd->gnm_cmd.beginptr = cmd->gnm_cmd.cmdptr = pm4;
    cmd->gnm_cmd.endptr = pm4 + 1024;
    VkPs4Image image = {0}; image.is_depth_target = true;
    VkPs4ImageView view = {0}; view.image = &image;
    view.gnm_drt_view.zreadbase256b = view.gnm_drt_view.zwritebase256b = 0x10000;
    VkPs4ImageView *views[] = {&view};
    VkPs4Framebuffer fb = {0}; fb.attachment_count = 1; fb.attachments = views;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {0}; subpass.pDepthStencilAttachment = &ref;
    VkPs4RenderPass pass = {0}; pass.subpass_count = 1; pass.subpasses = &subpass;
    cmd->current_render_pass.pass = &pass;
    cmd->current_render_pass.framebuffer = &fb;
    const GnmZFormat formats[] = {GNM_Z_32_FLOAT, GNM_Z_16, GNM_Z_24, GNM_Z_32_FLOAT};
    const uint32_t values[] = {0x1e9, 0xf0, 0xe8, 0x1e9};
    for (unsigned i = 0; i < 4; ++i) {
        cmd->gnm_cmd.cmdptr = pm4;
        view.gnm_drt_view.zinfo.format = formats[i];
        /* Bias before the render-pass binding must also get the right units. */
        vk_ps4_CmdSetDepthBias((VkCommandBuffer)cmd, 0.05f, 0.0f, 0.20f);
        vk_ps4_bind_subpass_targets(cmd);
        assert(register_value(pm4, cmd->gnm_cmd.cmdptr, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL) == values[i]);
        assert(register_value(pm4, cmd->gnm_cmd.cmdptr, R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE) == vk_ps4_fui(3.2f));
        assert(register_value(pm4, cmd->gnm_cmd.cmdptr, R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE) == vk_ps4_fui(3.2f));
        assert(register_value(pm4, cmd->gnm_cmd.cmdptr, R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET) == vk_ps4_fui(.05f));
        assert(register_value(pm4, cmd->gnm_cmd.cmdptr, R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET) == vk_ps4_fui(.05f));
        assert(register_value(pm4, cmd->gnm_cmd.cmdptr, R_028B7C_PA_SU_POLY_OFFSET_CLAMP) == 0);
        assert((register_value(pm4, cmd->gnm_cmd.cmdptr, R_028040_DB_Z_INFO) & 3) == formats[i]);
        printf("PASS production attachment/bias PM4: Z format=%u bias format=0x%x slope=3.2 constant=.05\n", formats[i], values[i]);
    }
    cmd->gnm_cmd.cmdptr = pm4;
    subpass.pDepthStencilAttachment = NULL;
    vk_ps4_bind_subpass_targets(cmd);
    assert(register_value(pm4, cmd->gnm_cmd.cmdptr, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL) == 0);
    puts("PASS depth detach resets bias format; no assumed platform default");
    free(cmd);
}
