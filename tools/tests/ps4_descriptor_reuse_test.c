/* Production ICD regression: immutable dynamic tables, not a model thereof. */
#include "vk_ps4_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vk_ps4_log(const char *fmt, ...) { (void)fmt; }
void sceGnmWriteMsg(GnmMessageSeverity severity, const char *message) {
    (void)severity; fprintf(stderr, "%s\n", message); abort();
}

int main(void) {
    VkPs4CommandBuffer *cmd = calloc(1, sizeof(*cmd));
    assert(cmd);
    cmd->dynamic_descriptor_tables = calloc(
        VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS * VK_PS4_MAX_DYNAMIC_DESCRIPTORS,
        sizeof(GnmBuffer));
    assert(cmd->dynamic_descriptor_tables);
    VkPs4DescriptorSetLayout dynamic_layout = {0}, static_layout = {0};
    dynamic_layout.dynamic_descriptor_count = 1;
    VkPs4PipelineLayout layout = {0};
    VkPs4DescriptorSetLayout *set_layouts[2];
    layout.set_layouts = set_layouts;
    layout.set_layout_count = 2;
    layout.set_layouts[0] = &dynamic_layout;
    layout.set_layouts[1] = &static_layout;
    GnmBuffer source = {0};
    sceGnmBufSetBaseAddress(&source, (void *)(uintptr_t)0x200010000ULL);
    VkPs4DescriptorBinding binding = {0};
    binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.count = 1;
    binding.descriptor_stride = sizeof(source);
    binding.table_base = (uint8_t *)&source;
    VkPs4DescriptorSet dynamic_set = {0}, material_a = {0}, material_b = {0};
    dynamic_set.layout = &dynamic_layout;
    dynamic_set.bindings = &binding;
    dynamic_set.binding_count = 1;
    material_a.layout = material_b.layout = &static_layout;
    VkDescriptorSet sets[2] = {(VkDescriptorSet)&dynamic_set, (VkDescriptorSet)&material_a};
    uint32_t offset = 256;
    vk_ps4_CmdBindDescriptorSets((VkCommandBuffer)cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (VkPipelineLayout)&layout, 0, 2, sets, 1, &offset);
    assert(cmd->recording_error == VK_SUCCESS && cmd->dynamic_table_cursor == 1);
    GnmBuffer *first = cmd->graphics_dynamic_table;
    GnmBuffer saved_first = *first;
    assert(sceGnmBufGetBaseAddress(first) == (void *)(uintptr_t)0x200010100ULL);
    for (unsigned i = 0; i < 10000; ++i) {
        VkDescriptorSet material = (VkDescriptorSet)(i % 2 ? &material_a : &material_b);
        vk_ps4_CmdBindDescriptorSets((VkCommandBuffer)cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            (VkPipelineLayout)&layout, 1, 1, &material, 0, NULL);
    }
    assert(cmd->dynamic_table_cursor == 1 && cmd->graphics_dynamic_table == first);
    assert(!cmd->dynamic_table_overflow);
    offset = 512;
    vk_ps4_CmdBindDescriptorSets((VkCommandBuffer)cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (VkPipelineLayout)&layout, 0, 1, sets, 1, &offset);
    assert(cmd->dynamic_table_cursor == 2 && cmd->graphics_dynamic_table != first);
    assert(memcmp(first, &saved_first, sizeof(*first)) == 0);
    /* Same set handle, changed descriptor bytes: must allocate a new table. */
    sceGnmBufSetBaseAddress(&source, (void *)(uintptr_t)0x200020000ULL);
    vk_ps4_CmdBindDescriptorSets((VkCommandBuffer)cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (VkPipelineLayout)&layout, 0, 1, sets, 1, &offset);
    assert(cmd->dynamic_table_cursor == 3);
    assert(sceGnmBufGetBaseAddress(cmd->graphics_dynamic_table) ==
           (void *)(uintptr_t)0x200020200ULL);
    vk_ps4_CmdBindDescriptorSets((VkCommandBuffer)cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        (VkPipelineLayout)&layout, 0, 2, sets, 1, &offset);
    assert(cmd->dynamic_table_cursor == 4);
    assert(cmd->compute_dynamic_table != cmd->graphics_dynamic_table);
    /* Exact capacity: unchanged binds remain legal; changed binds fail closed. */
    cmd->dynamic_table_cursor = VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS;
    vk_ps4_CmdBindDescriptorSets((VkCommandBuffer)cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (VkPipelineLayout)&layout, 0, 1, sets, 1, &offset);
    assert(!cmd->dynamic_table_overflow);
    offset += 256;
    vk_ps4_CmdBindDescriptorSets((VkCommandBuffer)cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (VkPipelineLayout)&layout, 0, 1, sets, 1, &offset);
    assert(cmd->dynamic_table_overflow);
    assert(memcmp(first, &saved_first, sizeof(*first)) == 0);
    free(cmd->dynamic_descriptor_tables);
    free(cmd);
    puts("PASS: 10000 material rebinds use 1 immutable dynamic snapshot; offset/source changes, compute isolation and exhaustion retain safety");
    return 0;
}
