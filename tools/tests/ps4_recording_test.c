/* Link the production command implementation; GNM calls are counted at the
 * boundary. No real GPU completion or timing claim is made by this test. */
#include "vk_ps4_internal.h"
#include "vk_ps4_vertex_hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned base_calls, type_calls;
static const void *last_base;
static GnmIndexSize last_type;
void vk_ps4_log(const char *format, ...) { (void)format; }
void sceGnmDrawCmdSetIndexBuffer(GnmCommandBuffer *cmd, const void *buffer) {
    (void)cmd; ++base_calls; last_base = buffer;
}
void sceGnmDrawCmdSetIndexSize(GnmCommandBuffer *cmd, GnmIndexSize size,
                             GnmCachePolicy policy) {
    (void)cmd; (void)policy; ++type_calls; last_type = size;
}
/* Executing the tiny secondary below must not allocate an overflow segment. */
GnmError sceGnmDirectMemoryAllocate(GnmDirectMemory *mem, uint64_t size,
    uint64_t alignment, int32_t type, int32_t protection) {
    (void)mem; (void)size; (void)alignment; (void)type; (void)protection;
    abort();
}
GnmCommandBuffer sceGnmCmdInit(void *buffer, uint32_t size,
    GnmCommandCallbackFunc *cb, void *data) {
    (void)buffer; (void)size; (void)cb; (void)data; abort();
}

int main(void) {
    VkPs4CommandBuffer *cmd = calloc(1, sizeof(*cmd));
    VkPs4CommandBuffer *secondary = calloc(1, sizeof(*secondary));
    assert(cmd && secondary);
    uint32_t pm4[8] = {0}, secondary_pm4[1] = {0x12345678u};
    cmd->gnm_cmd.beginptr = cmd->gnm_cmd.cmdptr = pm4;
    cmd->gnm_cmd.endptr = pm4 + 8;
    unsigned char storage[128];
    VkPs4DeviceMemory memory = {0};
    memory.gnm_mem.mapped = storage;
    VkPs4Buffer buffer = {0};
    buffer.memory = &memory;
    buffer.create_info.size = sizeof(storage);
    for (unsigned i = 0; i < 10000; ++i)
        vk_ps4_CmdBindIndexBuffer((VkCommandBuffer)cmd, (VkBuffer)&buffer,
                                 0, VK_INDEX_TYPE_UINT16);
    assert(base_calls == 1 && type_calls == 1 && last_base == storage);
    vk_ps4_CmdBindIndexBuffer((VkCommandBuffer)cmd, (VkBuffer)&buffer,
                             16, VK_INDEX_TYPE_UINT32);
    assert(base_calls == 2 && type_calls == 2);
    assert(last_base == storage + 16 && last_type == GNM_INDEX_32);
    /* A secondary can overwrite CP state: the same primary bind must emit. */
    secondary->level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    secondary->gnm_cmd.beginptr = secondary_pm4;
    secondary->gnm_cmd.cmdptr = secondary_pm4 + 1;
    VkCommandBuffer secondary_handle = (VkCommandBuffer)secondary;
    vk_ps4_CmdExecuteCommands((VkCommandBuffer)cmd, 1, &secondary_handle);
    assert(pm4[0] == secondary_pm4[0] && !cmd->index_buffer_state_valid);
    vk_ps4_CmdBindIndexBuffer((VkCommandBuffer)cmd, (VkBuffer)&buffer,
                             16, VK_INDEX_TYPE_UINT32);
    assert(base_calls == 3 && type_calls == 3);
    /* Invalid and misaligned binds cannot preserve a valid shadow. */
    vk_ps4_CmdBindIndexBuffer((VkCommandBuffer)cmd, (VkBuffer)&buffer,
                             3, VK_INDEX_TYPE_UINT32);
    assert(!cmd->index_buffer_state_valid && !cmd->index_buffer.buffer);
    vk_ps4_CmdBindIndexBuffer((VkCommandBuffer)cmd, (VkBuffer)&buffer,
                             16, VK_INDEX_TYPE_UINT32);
    assert(base_calls == 4 && type_calls == 4);
    vk_ps4_CmdBindIndexBuffer((VkCommandBuffer)cmd, (VkBuffer)&buffer,
                             16, (VkIndexType)0x1234);
    assert(!cmd->index_buffer_state_valid && !cmd->index_buffer.buffer);
    /* Exact-sized prefixes with ASan catch any old 512-byte overread. */
    for (unsigned count = 1; count <= VK_PS4_MAX_VERTEX_DESCRIPTORS; ++count) {
        GnmBuffer *key = malloc(count * sizeof(*key));
        assert(key);
        memset(key, 0x53, count * sizeof(*key));
        const uint32_t first = vk_ps4_vertex_table_hash(key, count);
        assert(first == vk_ps4_vertex_table_hash(key, count));
        ((unsigned char *)key)[count * sizeof(*key) - 1] ^= 1;
        assert(first != vk_ps4_vertex_table_hash(key, count));
        free(key);
    }
    free(secondary); free(cmd);
    puts("PASS: 10000 identical index binds emit one base/type pair; changes, secondary invalidation, invalid binds and 1..32 vertex hash prefixes checked");
    return 0;
}
