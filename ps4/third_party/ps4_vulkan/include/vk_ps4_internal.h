#ifndef VK_PS4_INTERNAL_H
#define VK_PS4_INTERNAL_H

/*
 * vulkan-ps4 — internal header.
 *
 * Defines the dispatch table, object wrappers, and shared state for the
 * Vulkan 1.0 ICD over OpenGNM.
 *
 * All Vulkan handle types are wrapped in a VkPs4* struct that carries the
 * GNM state needed to translate Vulkan calls into PM4/sceGnm* calls.
 * The Vulkan handle (VkInstance, VkDevice, etc.) is a pointer to these
 * structs cast to the opaque handle type.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

/* vk_icd.h is only needed for the loader interface on host.
 * On PS4, we don't use the loader. */
#ifndef VK_USE_PLATFORM_PS4
#include <vulkan/vk_icd.h>
#endif

#include <gnmdriver.h>
#include <gnm_commandbuffer.h>
#include <gnm_drawcommandbuffer.h>
#include <gnm_rendertarget.h>
#include <gnm_depthrendertarget.h>
#include <gnm_texture.h>
#include <gnm_buffer.h>
#include <gnm_sampler.h>
#include <gnm_shader.h>
#include <gnm_shaderbinary.h>
#include <gnm_helpers.h>
#include <gnm_dataformat.h>

/* Publish CPU writes made through the PS4's write-combined direct-memory
 * mappings before a PM4 packet, fetch shader, or resource table can consume
 * them.  A compiler barrier is not sufficient for WC Garlic memory. */
static inline void vk_ps4_cpu_store_fence(void) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    __asm__ volatile("sfence" ::: "memory");
#endif
}

#ifdef __cplusplus
extern "C" {
#endif

/* === Version === */
#define VK_PS4_API_VERSION VK_API_VERSION_1_0
#define VK_PS4_DRIVER_VERSION VK_MAKE_VERSION(0, 1, 0)

/* === Breadcrumb logging (PS4 hardware debugging) === */
/* Opens a breadcrumb log file.  Default path on PS4: /data/vk_ps4_breadcrumb.log
 * On host: /tmp/vk_ps4_breadcrumb.log.  Call early in main() or CreateInstance. */
void vk_ps4_log_open(const char *path);
void vk_ps4_log_close(void);
/* Printf-style breadcrumb.  Flushes after every write so logs survive crashes. */
void vk_ps4_log(const char *fmt, ...);
/* Single-string breadcrumb (no varargs overhead). */
void vk_ps4_log_raw(const char *msg);
bool vk_ps4_log_is_open(void);

/* Convenience macro for logging function entry */
#define VK_PS4_LOG_ENTRY() \
    vk_ps4_log_raw(__func__)

/* Log with VkResult */
#define VK_PS4_LOG_VR(call_name, result) \
    vk_ps4_log("%s -> %d", (call_name), (int)(result))

/* === Memory types === */
/* PS4 has two memory types:
 *   0 = Onion  (CPU-coherent, GPU-visible — for staging)
 *   1 = Garlic (GPU-local, WC — for render targets, textures, buffers)
 */
#define VK_PS4_MEMORY_TYPE_ONION  0
#define VK_PS4_MEMORY_TYPE_GARLIC 1
#define VK_PS4_MEMORY_TYPE_COUNT  2

/* === Object type enum === */
enum VkPs4ObjectType {
    VK_PS4_OBJ_INSTANCE,
    VK_PS4_OBJ_PHYSICAL_DEVICE,
    VK_PS4_OBJ_DEVICE,
    VK_PS4_OBJ_QUEUE,
    VK_PS4_OBJ_COMMAND_POOL,
    VK_PS4_OBJ_COMMAND_BUFFER,
    VK_PS4_OBJ_DEVICE_MEMORY,
    VK_PS4_OBJ_BUFFER,
    VK_PS4_OBJ_BUFFER_VIEW,
    VK_PS4_OBJ_IMAGE,
    VK_PS4_OBJ_IMAGE_VIEW,
    VK_PS4_OBJ_RENDER_PASS,
    VK_PS4_OBJ_FRAMEBUFFER,
    VK_PS4_OBJ_SHADER_MODULE,
    VK_PS4_OBJ_PIPELINE_LAYOUT,
    VK_PS4_OBJ_PIPELINE,
    VK_PS4_OBJ_DESCRIPTOR_SET_LAYOUT,
    VK_PS4_OBJ_DESCRIPTOR_POOL,
    VK_PS4_OBJ_DESCRIPTOR_SET,
    VK_PS4_OBJ_FENCE,
    VK_PS4_OBJ_SEMAPHORE,
    VK_PS4_OBJ_EVENT,
    VK_PS4_OBJ_QUERY_POOL,
    VK_PS4_OBJ_SWAPCHAIN_KHR,
    VK_PS4_OBJ_PIPELINE_CACHE,
};
typedef enum VkPs4ObjectType VkPs4ObjectType;

/* Forward declarations — many structs reference each other */
typedef struct VkPs4Instance VkPs4Instance;
typedef struct VkPs4PhysicalDevice VkPs4PhysicalDevice;
typedef struct VkPs4Device VkPs4Device;
typedef struct VkPs4Queue VkPs4Queue;
typedef struct VkPs4CommandPool VkPs4CommandPool;
typedef struct VkPs4CommandBuffer VkPs4CommandBuffer;
typedef struct VkPs4DeviceMemory VkPs4DeviceMemory;
typedef struct VkPs4Buffer VkPs4Buffer;
typedef struct VkPs4BufferView VkPs4BufferView;
typedef struct VkPs4Image VkPs4Image;
typedef struct VkPs4ImageView VkPs4ImageView;
typedef struct VkPs4RenderPass VkPs4RenderPass;
typedef struct VkPs4Framebuffer VkPs4Framebuffer;
typedef struct VkPs4ShaderModule VkPs4ShaderModule;
typedef struct VkPs4PipelineLayout VkPs4PipelineLayout;
typedef struct VkPs4Pipeline VkPs4Pipeline;
typedef struct VkPs4DescriptorSetLayout VkPs4DescriptorSetLayout;
typedef struct VkPs4DescriptorPool VkPs4DescriptorPool;
typedef struct VkPs4DescriptorSet VkPs4DescriptorSet;
typedef struct VkPs4Fence VkPs4Fence;
typedef struct VkPs4Semaphore VkPs4Semaphore;
typedef struct VkPs4Event VkPs4Event;
typedef struct VkPs4QueryPool VkPs4QueryPool;
typedef struct VkPs4Swapchain VkPs4Swapchain;
typedef struct VkPs4PipelineCache VkPs4PipelineCache;

/* === Instance === */
struct VkPs4Instance {
    VkPs4ObjectType type;
    VkInstanceCreateInfo create_info;
    VkAllocationCallbacks allocator;
    VkPs4PhysicalDevice *physical_device;  /* cached, freed in DestroyInstance */
};

/* === Physical device === */
struct VkPs4PhysicalDevice {
    VkPs4ObjectType type;
    VkPs4Instance *instance;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkPhysicalDeviceFeatures features;
};

/* === Device === */
#define VK_PS4_MAX_QUEUES 16

/* B39 pipelined submission.
 *
 * B38 and earlier drained the GPU inside every vkQueueSubmit: one shared
 * epilogue DCB carried the completion EOP and the CPU spun on its label
 * before the call returned.  Frame time was therefore CPU + GPU with no
 * overlap, and every streaming upload became a full pipeline flush - which
 * is what the "don't wait, let GPU work in parallel" comment on the async
 * upload path in vk_context.cpp was silently losing.
 *
 * A completion slot owns its own epilogue DCB and its own GPU-written label,
 * so several submissions can be outstanding without the CPU ever overwriting
 * PM4 the command processor may still be fetching.  A submission hands out a
 * ticket (slot index + expected label value); fences and semaphores store the
 * ticket instead of being marked signalled immediately, and resolve it when
 * they are queried.  Slots are recycled in issue order, which is also GPU
 * completion order on a single graphics ring.
 *
 * VK_PS4_SUBMIT_MODE_SERIAL restores the B38 behaviour exactly and is
 * selectable at runtime with WOWEE_VK_SUBMIT_MODE=serial. */
#define VK_PS4_MAX_SUBMIT_SLOTS 8u

enum VkPs4SubmitMode {
    VK_PS4_SUBMIT_MODE_SERIAL = 0,     /* B38: wait for the GPU inside submit */
    VK_PS4_SUBMIT_MODE_PIPELINED = 1,  /* B39 default: ticket-based completion */
};
typedef enum VkPs4SubmitMode VkPs4SubmitMode;

typedef struct VkPs4CompletionSlot {
    uint32_t *dcb;              /* epilogue DCB storage for this slot */
    uint32_t dcb_dwords;        /* usable dwords, excluding the label word */
    volatile uint32_t *label;   /* GPU-written completion label */
    uint32_t value;             /* value the GPU writes when this batch ends */
    bool issued;                /* a submission is outstanding on this slot */
} VkPs4CompletionSlot;

/* A handle to one outstanding submission.  slot_index is one-based so a
 * zeroed structure means "no pending GPU work". */
typedef struct VkPs4CompletionTicket {
    uint32_t slot_index;        /* 1 + index into gnm_submit_slots, 0 = none */
    uint32_t value;             /* label value this ticket waits for */
} VkPs4CompletionTicket;

struct VkPs4Device {
    VkPs4ObjectType type;
    VkPs4PhysicalDevice *physical_device;
    VkDeviceCreateInfo create_info;
    VkAllocationCallbacks allocator;
    /* Cached queues — allocated in CreateDevice, freed in DestroyDevice */
    VkPs4Queue *queues[VK_PS4_MAX_QUEUES];
    uint32_t queue_count;
    /* GNM state */
    bool gnm_initialized;
    /* Device-wide GNM init command buffer backing store.
     * Allocated from Garlic direct memory so the GPU can execute the
     * sceGnmDrawInitDefaultHardwareState preamble submitted at CreateDevice.
     * Held in the device so DestroyDevice can release it after the GPU is
     * quiesced. */
    GnmDirectMemory gnm_init_mem;
    uint32_t *gnm_init_cmd;
    uint32_t gnm_init_cmd_dwords;
    /* Epilogue command buffer used by QueueSubmit to emit EOP event writes
         * for fence/semaphore signaling.  Reused across submits — only one
         * submit is in flight at a time because QueueSubmit is serialized. */
    GnmDirectMemory gnm_epilogue_mem;
    uint32_t *gnm_epilogue_cmd;
    uint32_t gnm_epilogue_cmd_dwords;
    /* Queue submissions are deliberately serialized on the PS4 backend.
     * The last dword of gnm_epilogue_cmd is used as a GPU-written completion
     * label; the monotonically increasing value prevents a stale write from
     * making a later submit look complete.  Serial completion is conservative
     * but, importantly, means the single epilogue DCB is never overwritten
     * while the GPU can still be fetching it. */
    uint32_t gnm_completion_value;
    uint32_t gnm_compute_trace_count;
    bool gnm_device_lost;
    /* B39: completion slot ring.  gnm_submit_ring_mem backs every slot's DCB
     * and label in one Garlic allocation; slot 0 aliases the legacy epilogue
     * buffer above so the serial path and DestroyDevice are unchanged when
     * the ring could not be allocated.  gnm_submit_newest holds the ticket of
     * the most recent submission, which is what Queue/DeviceWaitIdle wait on. */
    VkPs4SubmitMode gnm_submit_mode;
    GnmDirectMemory gnm_submit_ring_mem;
    VkPs4CompletionSlot gnm_submit_slots[VK_PS4_MAX_SUBMIT_SLOTS];
    uint32_t gnm_submit_slot_count;   /* 0 = ring unavailable, serial only */
    uint32_t gnm_submit_next_slot;    /* round-robin cursor */
    VkPs4CompletionTicket gnm_submit_newest;
    /* VideoOut owns its scanout allocation independently of the graphics
     * queue.  Once a flip has been submitted, a GPU-idle checkpoint alone is
     * not sufficient to prove that the display engine stopped reading it.
     * Keep an explicit device-wide latch so DestroyDevice can fail closed if
     * a swapchain reports an unconfirmed/pending flip. */
    bool gnm_present_in_progress;
    bool gnm_present_uncertain;
    /* Embedded clear pixel shader for tiled RT clears.
     * The clear PS outputs a UBO vec4 to MRT0, enabling draw-based
     * clears on tiled surfaces where FillMemory doesn't work.  Its
     * executable code lives in aligned Garlic direct memory. */
    void *clear_ps_binary;
    GnmDirectMemory clear_ps_code_mem;
    GnmPsStageRegisters clear_ps_regs;
    uint8_t clear_ps_table_reg;
    bool clear_ps_ready;
    GnmDirectMemory clear_vs_code_mem;
    GnmVsStageRegisters clear_vs_regs;
    bool clear_vs_ready;
};

/* === Queue === */
/* Queue family indices — must match GetPhysicalDeviceQueueFamilyProperties */
#define VK_PS4_QUEUE_FAMILY_GRAPHICS 0
#define VK_PS4_QUEUE_FAMILY_COMPUTE  1
#define VK_PS4_NUM_QUEUE_FAMILIES    2

/* Fixed-size diagnostics; no GPU resources or synchronization state. */
typedef struct VkPs4QueuePerformance {
    uint64_t window_start_us;
    uint64_t samples;
    uint64_t user_bytes;
    uint64_t user_dcbs;
    uint64_t native_us;
    uint64_t completion_wait_us;
    uint64_t max_completion_wait_us;
    uint64_t polls;
    uint64_t max_poll_sleep_us;
} VkPs4QueuePerformance;

struct VkPs4Queue {
    VkPs4ObjectType type;
    VkPs4Device *device;
    uint32_t family_index;
    VkQueueFlags flags;  /* cached from family properties */
    /* Submit serialization — opaque pointer to platform-specific mutex */
    void *submit_mutex;
    VkPs4QueuePerformance performance;
};

/* === Command pool / buffer === */
#define VK_PS4_MAX_VERTEX_BINDINGS 16
#define VK_PS4_MAX_VERTEX_DESCRIPTORS 32
#define VK_PS4_MAX_DESCRIPTOR_SETS 4
#define VK_PS4_MAX_DESCRIPTOR_BINDINGS 64
#define VK_PS4_MAX_DYNAMIC_DESCRIPTORS_PER_SET 256
/* Every recorded draw must retain an immutable GPU-visible V# descriptor
 * table until submission completes. M6.2.9 records at most 512 world draws;
 * 1024 snapshots leave headroom for pipeline rebinds and diagnostic draws. */
/* wow_ps B4: a world frame binds far more vertex/descriptor tables than the
 * login screen the original sizes were tuned on; an overflow rejects the whole
 * frame. Four times the headroom costs a few MB of direct memory per command
 * buffer. */
#define VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS 4096
#define VK_PS4_MAX_DYNAMIC_DESCRIPTORS 256
#define VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS 2048
#define VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL 256
/* B20: fixed backing allocation; never relocate a recording whose spilled
 * constants contain absolute DCB addresses. B19 exhausted 1 MiB in a world
 * frame. Four MiB provides bounded headroom while visibility work is reduced. */
#define VK_PS4_CMD_BUFFER_ALLOCATION_SIZE (4u * 1024u * 1024u)
/* PM4 indirect-buffer size is a 20-bit dword count: 4 MiB minus one
 * dword. Keep the final allocated word outside the recordable stream. */
#define VK_PS4_CMD_BUFFER_SIZE GNM_INDIRECT_BUFFER_MAX_BYTESIZE

struct VkPs4CommandPool {
    VkPs4ObjectType type;
    VkPs4Device *device;
    uint32_t queue_family_index;
    VkCommandPoolCreateFlags flags;
    /* Track allocated command buffers for cleanup on DestroyCommandPool */
    VkPs4CommandBuffer *command_buffers[VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL];
    uint32_t command_buffer_count;
    /* Free list for command buffer reuse — reduces allocation overhead.
     * When FreeCommandBuffers is called, freed command buffers are kept
     * here instead of being returned to the allocator.  AllocateCommandBuffers
     * reuses them from this list first. */
    VkPs4CommandBuffer *free_list[VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL];
    uint32_t free_count;
};

#define VK_PS4_MAX_PM4_SEGMENTS 8u

struct VkPs4CommandBuffer {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4CommandPool *pool;
    VkCommandBufferLevel level;
    /* GNM command buffer — the actual PM4 packet storage */
    GnmCommandBuffer gnm_cmd;
    uint32_t *pm4_buffer;      /* backing store */
    /* Submitted PM4 must live in mapped direct memory on physical PS4.
     * Ordinary allocator/malloc memory passes host tests but is not a valid
     * sceGnmSubmitCommandBuffers DCB on FW 5.05. */
    GnmDirectMemory pm4_mem;
    /* Each DCB respects the native <4 MiB size limit. Additional segments keep
     * embedded GPU addresses stable and are reused only after the owning fence. */
    GnmDirectMemory pm4_extra[VK_PS4_MAX_PM4_SEGMENTS - 1u];
    uint32_t pm4_segment_used[VK_PS4_MAX_PM4_SEGMENTS];
    uint32_t pm4_segment_count;

    uint32_t pm4_buffer_size;  /* in dwords */
    uint32_t pm4_used;         /* in dwords */
    uint32_t pm4_high_water;   /* lifetime high-water mark, in dwords */
    uint32_t pm4_world_recordings; /* sparse capacity diagnostics */
    uint32_t compute_dispatch_count; /* B18: trace first compute submissions */
    bool is_recording;
    VkResult recording_error; /* unsupported/invalid meta operation: never submit */
    bool is_begin;
    VkViewport viewport0;
    VkRect2D scissor0;
    bool viewport0_valid;
    bool scissor0_valid;
    /* Current render pass state */
    struct {
        VkPs4RenderPass *pass;
        VkPs4Framebuffer *framebuffer;
        VkRect2D render_area;
        uint32_t current_subpass;  /* index into pass->subpasses */
        VkClearValue clear_values[16];  /* deep-copied from CmdBeginRenderPass */
        uint32_t clear_value_count;
        /* For imageless framebuffers: attachment views from
         * VkRenderPassAttachmentBeginInfo at beginRenderPass time. */
        VkPs4ImageView *imageless_attachments[16];
        uint32_t imageless_attachment_count;
    } current_render_pass;
    /* Current pipeline */
    VkPs4Pipeline *current_pipeline;
    VkPs4PipelineLayout *graphics_descriptor_layout;
    uint32_t graphics_push_constants[2][32]; /* VS/PS 128-byte limit */
    bool graphics_push_valid[2];
    /* Bound vertex buffers */
    struct {
        VkBuffer buffer;
        VkDeviceSize offset;
    } vertex_buffers[VK_PS4_MAX_VERTEX_BINDINGS];
    uint32_t vertex_binding_count;
    /* GnmBuffer descriptors consumed by the generated fetch shader.  The
     * descriptor count follows vertex attributes/semantics, not Vulkan buffer
     * bindings (several interleaved attributes may share one binding).  Keep
     * an immutable per-bind snapshot arena in mapped Garlic direct memory:
     * later vkCmdBindVertexBuffers calls must never overwrite a table already
     * referenced by recorded PM4. Passing ordinary malloc memory, or reusing
     * one mutable table, faults/corrupts retail hardware. */
    GnmDirectMemory vertex_table_mem;
    GnmBuffer *gnm_vertex_buffers;
    /* CPU-cached mirror used only as the immutable snapshot lookup key.
     * Reading WC Garlic memory on the CPU is extremely expensive on retail
     * PS4; the old cache compared candidates directly against
     * vertex_table_mem.mapped and turned shadow recording into a hundreds-of-
     * milliseconds readback loop.  The fetch shader still receives only the
     * Garlic table below. */
    GnmBuffer *vertex_table_cpu_keys;
    uint32_t vertex_table_slot_cursor;
    uint32_t vertex_table_slot_capacity;
    /* Slot + 1, open-addressed. Reset only when recording storage is reusable. */
    uint32_t vertex_table_hash[8192];
    bool vertex_table_overflow;
    uint32_t vertex_descriptor_count;
    bool vertex_buffers_dirty;  /* re-emit VB table on next draw */
    /* Dynamic buffer descriptors are command state because offsets are bound
     * at record time and descriptor sets may be updated later. */
    GnmDirectMemory dynamic_table_mem;
    GnmBuffer *dynamic_descriptor_tables;
    uint32_t dynamic_table_cursor;
    bool dynamic_table_overflow;
    VkPs4DescriptorSet *bound_graphics_sets[VK_PS4_MAX_DESCRIPTOR_SETS];
    VkPs4DescriptorSet *bound_compute_sets[VK_PS4_MAX_DESCRIPTOR_SETS];
    uint32_t graphics_dynamic_offsets[VK_PS4_MAX_DESCRIPTOR_SETS]
                                     [VK_PS4_MAX_DYNAMIC_DESCRIPTORS_PER_SET];
    uint32_t compute_dynamic_offsets[VK_PS4_MAX_DESCRIPTOR_SETS]
                                    [VK_PS4_MAX_DYNAMIC_DESCRIPTORS_PER_SET];
    /* User-data emission is deferred to the draw or dispatch that needs it.
     * Vulkan lets an application bind descriptor sets and push constants
     * before the pipeline, and a pipeline change moves the same sets to the
     * new shaders' registers. Emitting at bind time dropped the first case
     * outright (B4 test 11: the character renderer binds its per-frame set
     * before vkCmdBindPipeline, so set 0 never reached the VS/PS and the
     * first scene draw read its camera UBO through an unwritten SGPR pair)
     * and left the second to the application. */
    GnmBuffer *graphics_dynamic_table;  /* latest snapshot for the graphics sets */
    GnmBuffer *compute_dynamic_table;
    bool graphics_tables_dirty;
    bool compute_tables_dirty;
    bool graphics_push_dirty[2];
    uint32_t compute_push_constants[32];
    bool compute_push_valid;
    bool compute_push_dirty;
    /* The pipeline the vertex table snapshot was built against: the table
     * follows the pipeline's vertex input, so a draw with another pipeline
     * rebuilds it first. */
    VkPs4Pipeline *vertex_table_pipeline;
    /* Bound index buffer */
    struct {
        VkBuffer buffer;
        VkDeviceSize offset;
        VkIndexType type;
    } index_buffer;
    /* Shadow stencil state for read-modify-write on dynamic stencil commands.
     * Without this, each CmdSetStencil* would clobber the other fields of
     * DB_STENCILREFMASK / DB_STENCILREFMASK_BF. */
    uint32_t stencil_refmask_front;   /* DB_STENCILREFMASK shadow */
    uint32_t stencil_refmask_back;    /* DB_STENCILREFMASK_BF shadow */
    bool stencil_shadow_valid;        /* initialized from pipeline bind */
};

/* === Memory === */
struct VkPs4DeviceMemory {
    VkPs4ObjectType type;
    VkPs4Device *device;
    uint32_t memory_type_index;
    GnmDirectMemory gnm_mem;
    VkDeviceSize size;
    void *mapped_ptr;
    VkDeviceSize mapped_offset;
    VkDeviceSize mapped_size;
};

/* === Buffer === */
struct VkPs4Buffer {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkBufferCreateInfo create_info;
    GnmBuffer gnm_buffer;
    VkPs4DeviceMemory *memory;
    VkDeviceSize memory_offset;
};

/* === Buffer View === */
struct VkPs4BufferView {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4Buffer *buffer;
    GnmBuffer gnm_buffer;       /* V# descriptor for texel buffer access */
    VkFormat format;
    VkDeviceSize offset;
    VkDeviceSize range;
};

/* === Image === */
struct VkPs4Image {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkImageCreateInfo create_info;
    GnmTexture gnm_texture;
    GnmRenderTarget gnm_rt;        /* if used as render target */
    GnmDepthRenderTarget gnm_drt;  /* if used as depth target */
    bool is_render_target;
    bool is_depth_target;
    /* Swapchain image tracking: if this image is a swapchain buffer,
     * these fields identify the video out handle and buffer index so
     * that CmdBeginRenderPass can emit WaitUntilSafeForRendering. */
    bool is_swapchain_image;
    int32_t video_out_handle;
    uint32_t swapchain_buffer_index;
    /* EXPERIMENT (black-screen investigation): for a swapchain image, real
     * rasterized draws never change the buffer VideoOut actually scans out,
     * while CPU/DMA-style writes (FillMemory) do - suggesting CB (color
     * buffer) export may not reach memory sourced from
     * sceGnmVideoOutGetBuffer() specifically, as opposed to the normal Garlic
     * allocator every other render target/texture uses. `memory` below is
     * redirected to a same-size, same-tile-mode shadow buffer from the
     * normal allocator so all rendering (clears, real draws) targets that
     * instead; `videoout_buffer` keeps the real VideoOut pointer so
     * QueuePresentKHR can memcpy the finished frame into it right before the
     * flip. Only ever set for swapchain images. */
    void *videoout_buffer;
    VkPs4DeviceMemory *memory;
    VkDeviceSize memory_offset;
    VkImageLayout layout;
};

/* === Image view === */
struct VkPs4ImageView {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4Image *image;
    VkImageViewCreateInfo create_info;
    GnmTexture gnm_view;           /* texture view descriptor */
    GnmRenderTarget gnm_rt_view;
    GnmDepthRenderTarget gnm_drt_view;
};

/* === Render pass === */
struct VkPs4RenderPass {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkRenderPassCreateInfo create_info;
    uint32_t attachment_count;
    VkAttachmentDescription *attachments;
    uint32_t subpass_count;
    VkSubpassDescription *subpasses;
    /* Deep-copied subpass internal arrays — one slot per subpass.
     * Kept alive for the render pass's lifetime because CmdBeginRenderPass
     * and CmdNextSubpass dereference them after the caller's pCreateInfo
     * has been freed. */
    VkAttachmentReference **subpass_input_attachments;   /* [subpass_count], NULL slots ok */
    VkAttachmentReference **subpass_color_attachments;   /* [subpass_count] */
    VkAttachmentReference **subpass_resolve_attachments; /* [subpass_count], NULL slots ok */
    VkAttachmentReference **subpass_depth_stencil;       /* [subpass_count], NULL slots ok */
    uint32_t **subpass_preserve_attachments;             /* [subpass_count], NULL slots ok */
    uint32_t subpass_dependency_count;
    VkSubpassDependency *dependencies;
};

/* === Framebuffer === */
struct VkPs4Framebuffer {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4RenderPass *render_pass;
    VkFramebufferCreateInfo create_info;
    uint32_t attachment_count;
    VkPs4ImageView **attachments;   /* NULL for imageless framebuffers */
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    bool imageless;                 /* VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT_KHR */
};

/* === Shader module === */
struct VkPs4ShaderModule {
    VkPs4ObjectType type;
    VkPs4Device *device;
    /* Compiled GCN shader binary (GnmShaderFileHeader + stage header + code) */
    void *binary;
    size_t binary_size;
    GnmShaderMetadata metadata;
    bool has_metadata;
};

/* === Pipeline cache ===
 * The pipeline cache stores compiled shader binaries keyed by a hash
 * of the SPIR-V code + stage.  This allows:
 * 1. Fast pipeline creation when the same shader is used again
 * 2. Persistence via GetPipelineCacheData / CreatePipelineCache(initialData)
 *
 * Cache format (after the 32-byte Vulkan header):
 *   uint32_t entry_count
 *   For each entry:
 *     uint64_t hash          (SPIR-V hash + stage)
 *     uint32_t stage         (VkShaderStageFlagBits)
 *     uint32_t spirv_size    (SPIR-V code size for verification)
 *     uint32_t binary_size   (compiled GCN binary size)
 *     uint8_t  spirv_hash[16] (SHA-1 truncated to 16 bytes of SPIR-V)
 *     void     binary_data[binary_size]
 */
#define VK_PS4_PIPELINE_CACHE_MAX_ENTRIES 256
typedef struct {
    uint64_t hash;          /* hash of stage + spirv code */
    uint32_t stage;         /* VkShaderStageFlagBits */
    uint32_t spirv_size;    /* original SPIR-V size */
    uint32_t binary_size;   /* compiled binary size */
    void *binary;           /* compiled GCN shader binary */
} VkPs4PipelineCacheEntry;

struct VkPs4PipelineCache {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkAllocationCallbacks allocator;
    VkPs4PipelineCacheEntry entries[VK_PS4_PIPELINE_CACHE_MAX_ENTRIES];
    uint32_t entry_count;
};

/* === Pipeline layout / descriptor set layout === */
struct VkPs4DescriptorSetLayout {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkDescriptorSetLayoutCreateInfo create_info;
    VkDescriptorSetLayoutBinding *bindings;
    uint32_t binding_count;
    /* VK_EXT_descriptor_indexing: per-binding flags */
    VkDescriptorBindingFlags *binding_flags;
    /* Variable descriptor count: the last binding's max count can vary
     * at descriptor set allocation time. */
    uint32_t variable_descriptor_binding;  /* binding index, or UINT32_MAX */
    /* RADV-compatible byte layout consumed by PSBC.  These arrays follow
     * declaration order while each entry retains its Vulkan binding number. */
    uint32_t binding_offsets[VK_PS4_MAX_DESCRIPTOR_BINDINGS];
    uint32_t binding_strides[VK_PS4_MAX_DESCRIPTOR_BINDINGS];
    uint32_t binding_dynamic_offsets[VK_PS4_MAX_DESCRIPTOR_BINDINGS];
    uint32_t table_size;
    uint32_t dynamic_descriptor_count;
};

struct VkPs4PipelineLayout {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPipelineLayoutCreateInfo create_info;
    VkPs4DescriptorSetLayout **set_layouts;
    uint32_t set_layout_count;
    VkPushConstantRange *push_constant_ranges;
    uint32_t push_constant_range_count;
};

/* === Descriptor / pipeline limits === */
#define VK_PS4_MAX_INPUT_USAGE_SLOTS 32
/* FW 5.05 Garlic mappings used by GNM live in the 0x2_xxxxxxxx GPU-VA
 * window.  ACO's GFX7 descriptor ABI carries only the low dword in the
 * direct set-pointer SGPR and reconstructs this high dword at compile time. */
#define VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI 2u
#define VK_PS4_PSBC_COMBINED_IMAGE_BYTES 32u
#define VK_PS4_PSBC_COMBINED_SAMPLER_OFFSET 32u
#define VK_PS4_PSBC_COMBINED_TABLE_BYTES 48u
#define VK_PS4_PSBC_DYNAMIC_DESCRIPTOR_API_SLOT 0xffu
#define VK_PS4_PSBC_PUSH_CONSTANT_API_SLOT 0xfeu

/* === Pipeline === */
struct VkPs4Pipeline {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPipelineBindPoint bind_point;
    /* Graphics pipeline */
    VkPs4ShaderModule *vs_module;
    VkPs4ShaderModule *fs_module;
    VkPs4ShaderModule *gs_module;
    VkPs4ShaderModule *tcs_module;
    VkPs4ShaderModule *tes_module;
    VkPs4ShaderModule *cs_module;
    /* Compiled GCN shader binaries — kept alive for the pipeline's lifetime
     * because the stage registers contain GPU addresses that point into
     * these buffers.  Freed in DestroyPipeline. */
    void *vs_binary;
    void *ps_binary;
    void *gs_binary;
    void *tcs_binary;
    void *tes_binary;
    void *cs_binary;
    /* Executable shader code is copied out of the compiler container into
     * 64 KiB-aligned Garlic allocations.  GCN program addresses discard the
     * low eight address bits; executing directly from a merely 16-byte-aligned
     * compiler container can therefore jump before the shader and fault the
     * GPU.  The container remains alive separately for metadata tables. */
    GnmDirectMemory vs_code_mem;
    GnmDirectMemory ps_code_mem;
    GnmDirectMemory gs_code_mem;
    GnmDirectMemory tcs_code_mem;
    GnmDirectMemory tes_code_mem;
    GnmDirectMemory cs_code_mem;
    /* GNM shader stage registers */
    GnmVsStageRegisters vs_regs;
    GnmPsStageRegisters ps_regs;
    GnmCsStageRegisters cs_regs;
    /* Tessellation / geometry shader stage registers */
    GnmHsStageRegisters hs_regs;
    GnmLsStageRegisters ls_regs;
    GnmGsStageRegisters gs_regs;
    GnmEsStageRegisters es_regs;
    bool has_hs;  /* tessellation control shader (hull shader) */
    bool has_ls;  /* tessellation control shader (LS = VS before tess) */
    bool has_gs;  /* geometry shader */
    bool has_es;  /* geometry shader (ES = VS before GS) */
    bool has_ds_vs;  /* TES compiled as DS_VS (post-tessellation VS) */
    uint32_t tess_patch_control_points;  /* from VkPipelineTessellationStateCreateInfo */
    /* Pipeline state */
    VkPipelineVertexInputStateCreateInfo vertex_input_state;
    VkVertexInputBindingDescription *vertex_bindings;      /* deep copy */
    VkVertexInputAttributeDescription *vertex_attributes;  /* deep copy */
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
    VkPipelineRasterizationStateCreateInfo rasterization_state;
    VkPipelineColorBlendStateCreateInfo color_blend_state;
    VkPipelineColorBlendAttachmentState *blend_attachments;  /* deep copy */
    GnmBlendControl blend_controls[8];  /* pre-computed per RT slot */
    uint32_t blend_control_count;       /* number of valid blend_controls */
    uint32_t color_write_mask;          /* packed RT mask (4 bits per RT) */
    float blend_constants[4];
    bool has_blend_state;
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    VkPipelineViewportStateCreateInfo viewport_state;
#define VK_PS4_MAX_VIEWPORTS 16
    VkViewport static_viewports[VK_PS4_MAX_VIEWPORTS];
    VkRect2D static_scissors[VK_PS4_MAX_VIEWPORTS];
    uint32_t static_viewport_count;
    uint32_t static_scissor_count;
    bool dynamic_viewport;
    bool dynamic_scissor;
    bool dynamic_depth_bias;
    bool dynamic_line_width;
    VkPipelineMultisampleStateCreateInfo multisample_state;
    /* Fetch shader (generated from vertex input).  Executable fetch code is
     * GPU-addressed just like stage code and therefore must not live in the
     * ordinary host allocator on retail hardware. */
    GnmDirectMemory fetch_shader_mem;
    void *fetch_shader;
    size_t fetch_shader_size;
    bool has_fetch_shader;
    bool has_ps;          /* true if fragment shader was set */
    bool has_fetch_shader_slot;  /* true if fetch_shader_slot is valid */
    bool has_vb_table_slot;      /* true if vertex_buffer_table_slot is valid */
    /* User-data slot for fetch shader pointer (SUBPTR_FETCHSHADER) */
    uint32_t fetch_shader_slot;
    /* User-data slot for vertex buffer table (PTR_VERTEXBUFFERTABLE) */
    uint32_t vertex_buffer_table_slot;
    /* Input usage slot tables extracted from compiled shader binaries.
     * These map Vulkan binding numbers (apislot) to GNM user-data
     * registers (startregister) for each shader stage. */
    GnmInputUsageSlot vs_input_usage_slots[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t vs_input_usage_slot_count;
    GnmInputUsageSlot ps_input_usage_slots[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t ps_input_usage_slot_count;
    /* Vertex input semantics extracted from VS shader binary */
    GnmVertexInputSemantic vs_input_semantics[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t vs_input_semantic_count;
    /* Cross-stage raster linkage extracted from the compiler containers.
     * SPI_PS_INPUT_CNTL_n is not implied by the VS/PS program registers: it
     * must explicitly map every PS input semantic to the matching VS export
     * semantic before a draw reaches the interpolator. */
    GnmVertexExportSemantic
        vs_export_semantics[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t vs_export_semantic_count;
    GnmPixelInputSemantic
        ps_input_semantics[VK_PS4_MAX_INPUT_USAGE_SLOTS];
    uint32_t ps_input_semantic_count;
    /* Push constant inline register mapping — extracted from
     * IMM_ALUFLOATCONST input usage slots emitted by psbc.
     * Each entry maps a push constant dword index to a user-data
     * register for that shader stage. */
#define VK_PS4_MAX_PUSH_CONST_DWORDS 32  /* 128 bytes / 4 */
    struct {
        uint8_t dword_index;     /* push constant dword offset */
        uint8_t user_data_reg;   /* GNM user-data register */
    } vs_push_const_slots[VK_PS4_MAX_PUSH_CONST_DWORDS];
    uint32_t vs_push_const_slot_count;
    struct {
        uint8_t dword_index;
        uint8_t user_data_reg;
    } ps_push_const_slots[VK_PS4_MAX_PUSH_CONST_DWORDS];
    uint32_t ps_push_const_slot_count;
    struct {
        uint8_t dword_index;
        uint8_t user_data_reg;
    } cs_push_const_slots[VK_PS4_MAX_PUSH_CONST_DWORDS];
    uint32_t cs_push_const_slot_count;
    /* VS draw offset registers — extracted from IMM_ALUFLOATCONST slots
     * with special apislot values emitted by psbc.
     * 0xFE = base_vertex (vertexOffset), 0xFF = start_instance (firstInstance). */
    uint8_t vs_base_vertex_reg;      /* user-data reg for vertexOffset */
    uint8_t vs_start_instance_reg;   /* user-data reg for firstInstance */
    bool has_base_vertex_reg;
    bool has_start_instance_reg;
    /* Pre-computed depth/stencil GNM state (converted from VkPipelineDepthStencilStateCreateInfo) */
    GnmDepthStencilControl depth_stencil_control;
    bool has_depth_stencil_state;  /* true if pDepthStencilState was non-NULL */
    /* Stencil ref/mask register values (pre-computed for CmdBindPipeline) */
    uint32_t stencil_refmask;      /* DB_STENCILREFMASK (front) */
    uint32_t stencil_refmask_bf;   /* DB_STENCILREFMASK_BF (back) */
    uint32_t stencil_control;      /* DB_STENCIL_CONTROL (ops front+back) */
};

/* === Descriptor === */
typedef struct {
    VkDescriptorType type;
    uint32_t count;
    uint32_t binding_number;  /* Vulkan binding number (may be sparse) */
    uint32_t table_offset;
    uint32_t descriptor_stride;
    uint32_t dynamic_offset_offset;
    bool resources_allocated; /* true once resource arrays are alloc'd */
    uint8_t *table_base;
    /* PSBC may lower scalar descriptors to PTR_*TABLE input usages.  Keep
     * every potential table in mapped Garlic memory; ordinary host-heap
     * arrays are not GPU-addressable on retail PS4.  Combined image/sampler
     * bindings carve both arrays from this single allocation. */
    GnmDirectMemory resource_mem;
    /* Resource data — which array is valid depends on type */
    GnmBuffer *buffers;     /* UBO / SSBO / texel buffer */
    GnmTexture *textures;   /* sampled / storage image */
    GnmSampler *samplers;   /* sampler (incl. combined image sampler) */
} VkPs4DescriptorBinding;

void vk_ps4_release_descriptor_binding_resources(
    VkPs4DescriptorBinding *binding,
    const VkAllocationCallbacks *allocator
);
VkResult vk_ps4_allocate_descriptor_binding_resources(
    VkPs4DescriptorBinding *binding,
    const VkAllocationCallbacks *allocator
);

struct VkPs4DescriptorPool {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkDescriptorPoolCreateInfo create_info;
    /* Pool tracking: count allocated vs max, and a free list for reuse.
     * When VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT is set,
     * freed sets are kept in the free list for reuse instead of being
     * returned to the allocator.  This reduces allocation overhead for
     * apps that frequently allocate/free descriptor sets. */
    uint32_t sets_allocated;
    uint32_t sets_freed;
#define VK_PS4_MAX_POOLED_SETS 256
    /* Sets still owned by the pool but not individually freed.  Vulkan lets
     * applications destroy/reset a pool without vkFreeDescriptorSets, so the
     * pool must retain these handles to release their Garlic tables. */
    VkPs4DescriptorSet *active_sets[VK_PS4_MAX_POOLED_SETS];
    uint32_t active_count;
    VkPs4DescriptorSet *free_list[VK_PS4_MAX_POOLED_SETS];
    uint32_t free_count;
};

struct VkPs4DescriptorSet {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkPs4DescriptorPool *pool;
    VkPs4DescriptorSetLayout *layout;
    /* Host metadata follows this header in the same allocation. Most world
     * sets use 1-8 bindings; reserving all 64 per set exhausted CPU memory. */
    VkPs4DescriptorBinding *bindings;
    uint32_t binding_capacity;
    uint32_t binding_count;
    /* VK_EXT_descriptor_indexing: variable descriptor count for the
     * last binding (0 if layout doesn't use variable count). */
    uint32_t variable_descriptor_count;
    /* One contiguous RADV-compatible descriptor table per Vulkan set. */
    GnmDirectMemory descriptor_mem;
};

VkResult vk_ps4_allocate_descriptor_set_resources(
    VkPs4DescriptorSet *set, const VkAllocationCallbacks *allocator
);
void vk_ps4_release_descriptor_set_resources(VkPs4DescriptorSet *set);
/* Writes the bound sets' table pointers to the current pipeline's user-data
 * registers if anything changed since the last draw of that bind point. */
void vk_ps4_flush_descriptor_tables(VkPs4CommandBuffer *cmd, VkPipelineBindPoint bind_point);

/* === B39 pipelined submission (vk_ps4_queue.c) === */

/* Selected once from WOWEE_VK_SUBMIT_MODE; "serial" restores B38 behaviour.
 * Any other value (or none) selects the pipelined path when the completion
 * ring is available. */
VkPs4SubmitMode vk_ps4_queue_submit_mode(void);

/* Allocate/release the completion slot ring.  Called from CreateDevice after
 * the legacy epilogue buffer exists and from DestroyDevice once the GPU is
 * quiesced.  A failed allocation is not fatal: the device falls back to the
 * serial path. */
VkResult vk_ps4_queue_init_completion_ring(VkPs4Device *dev);
void vk_ps4_queue_release_completion_ring(VkPs4Device *dev);

/* True once the GPU has written the ticket's value to its slot label.  A
 * zeroed ticket (slot_index == 0) reads as complete. */
bool vk_ps4_queue_ticket_complete(VkPs4Device *dev,
                                  const VkPs4CompletionTicket *ticket);

/* Block until the ticket completes or the completion watchdog expires.
 * Returns false on timeout, which marks the device lost. */
bool vk_ps4_queue_ticket_wait(VkPs4Device *dev,
                              const VkPs4CompletionTicket *ticket);

/* Latch a resolved ticket into the object's signalled state.  Both are safe
 * to call on objects that carry no ticket. */
bool vk_ps4_sync_resolve_fence(VkPs4Fence *fence);
bool vk_ps4_sync_resolve_semaphore(VkPs4Semaphore *sem);
void vk_ps4_emit_address32_user_data(VkPs4CommandBuffer *cmd, GnmShaderStage stage,
                                   uint32_t reg, const void *address);

/* === Sync === */
struct VkPs4Fence {
    VkPs4ObjectType type;
    VkPs4Device *device;
    /* CPU-side fallback flag used when the device has no GNM epilogue
         * buffer (host tests) or before the first signal. */
    bool signaled;
    /* GPU signal label — allocated from Garlic direct memory.  The GPU
         * writes signal_value here via an EOP event write at the end of the
         * submit that signals this fence.  WaitForFences polls this label. */
    GnmDirectMemory label_mem;
    volatile uint32_t *label;
    uint32_t signal_value;
    /* B39: set by a pipelined submit that signals this fence.  The fence is
     * considered signalled once the referenced completion slot's label has
     * reached the ticket value; vk_ps4_sync_resolve_fence() performs that
     * check and latches the result. */
    VkPs4CompletionTicket pending;
};

struct VkPs4Semaphore {
    VkPs4ObjectType type;
    VkPs4Device *device;
    bool signaled;
    /* GPU signal label — same EOP mechanism as fences.  Wait semaphores
         * block the next submit's first command buffer with WaitMem until
         * the label matches signal_value. */
    GnmDirectMemory label_mem;
    volatile uint32_t *label;
    uint32_t signal_value;
    /* VK_KHR_timeline_semaphore: timeline semaphore support.
     * Timeline semaphores maintain a monotonically increasing counter
     * instead of a binary signaled/unsignaled state.  The counter is
     * software-managed (CPU-side) since the GPU EOP label only supports
     * 32-bit values and we need 64-bit timeline values. */
    bool is_timeline;
    uint64_t timeline_value;      /* current signaled value */
    /* B39: same ticket mechanism as VkPs4Fence.  pending_timeline_value is
     * the value timeline_value takes once the ticket completes, so a timeline
     * wait cannot observe a value the GPU has not reached. */
    VkPs4CompletionTicket pending;
    uint64_t pending_timeline_value;
};

struct VkPs4Event {
    VkPs4ObjectType type;
    VkPs4Device *device;
    bool signaled;
};

/* === Query pool === */
struct VkPs4QueryPool {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkQueryPoolCreateInfo create_info;
    /* GPU-visible memory for query results.
     * Each query slot stores a uint64_t result.
     * For occlusion queries: ZPASS count.
     * For timestamp queries: GPU timestamp value. */
    GnmDirectMemory gnm_mem;     /* full direct memory handle for release */
    void *result_buffer;         /* CPU-mapped pointer to result memory */
    uint64_t result_gpu_addr;    /* GPU address of result memory */
    VkDeviceSize result_size;    /* total size in bytes */
};

/* === Swapchain === */
struct VkPs4Swapchain {
    VkPs4ObjectType type;
    VkPs4Device *device;
    VkSwapchainCreateInfoKHR create_info;
    GnmVideoOut video_out;
    VkPs4Image *images;
    /* image_memory[i].gnm_mem now wraps shadow_memory[i] (owned, below),
     * not VideoOut's own buffer - see the comment on VkPs4Image::memory. */
    VkPs4DeviceMemory image_memory[GNM_VIDEO_OUT_MAX_BUFFERS];
    /* Owned, same-size Garlic allocations that rendering actually targets.
     * Freed in DestroySwapchainKHR alongside the rest of the swapchain's
     * resources. */
    GnmDirectMemory shadow_memory[GNM_VIDEO_OUT_MAX_BUFFERS];
    uint32_t image_count;
    uint32_t current_image;
    /* Per-image in-flight tracking for AcquireNextImageKHR.
     *
     * image_in_flight[] is the authoritative tracking — set to true
     * when an image is acquired and cleared when QueuePresentKHR
     * completes the flip.  This is independent of whether the caller
     * passed a fence, so semaphore-only sync patterns work correctly.
     *
     * image_fences[] stores the caller's fence (if provided) as an
     * optimization: when all images are in-flight, we wait on the
     * fence rather than busy-spinning.  May be NULL if the caller
     * used semaphore-only sync. */
    bool image_in_flight[GNM_VIDEO_OUT_MAX_BUFFERS];
    VkFence image_fences[GNM_VIDEO_OUT_MAX_BUFFERS];
    /* Set before sceVideoOutSubmitFlip and cleared only after both the flip
     * event and sceVideoOutIsFlipPending()==0 confirm display ownership. */
    bool flip_submission_uncertain;
    bool last_present_confirmed;
    /* B39 asynchronous present.
     *
     * B38 submitted a flip and then blocked on its VideoOut event, so every
     * frame paid a full scanout wait on the critical path even though the
     * console has more than one scanout buffer to work with. Here a present
     * submits the flip and returns; the event is collected later, by the
     * acquire that needs the buffer back or by the present that would exceed
     * the queue depth.
     *
     * pending_flips is a FIFO of buffer indices whose flip has been submitted
     * but whose event has not been observed yet. displayed_image is the buffer
     * the display engine is currently scanning out: when a flip event
     * arrives, the buffer it replaces is what becomes reusable, so that one
     * index is what a reaped event frees. */
    uint32_t pending_flips[GNM_VIDEO_OUT_MAX_BUFFERS];
    uint32_t pending_flip_count;
    /* Successful-present interval timings, CPU wall time in microseconds. */
    uint64_t perf_present_samples, perf_render_wait_us;
    uint64_t perf_flip_wait_us, perf_flip_submit_us, perf_present_us;
    uint64_t perf_present_max_us;
    uint32_t displayed_image;      /* index, or image_count when none yet */
    bool async_flip_enabled;
};

/* === Utility macros === */
#define VK_PS4_CAST(handle) ((void*)(handle))
#define VK_PS4_TO_OBJ(handle) ((VkPs4ObjectType*)(handle))
#define VK_PS4_CHECK_OBJ(handle, expected_type) \
    (VK_PS4_TO_OBJ(handle) && *(VK_PS4_TO_OBJ(handle)) == (expected_type))

/* === Allocator helpers === */
void *vk_ps4_alloc(const VkAllocationCallbacks *alloc, size_t size, size_t alignment);
void *vk_ps4_alloc_zero(const VkAllocationCallbacks *alloc, size_t size, size_t alignment);
void vk_ps4_free(const VkAllocationCallbacks *alloc, void *ptr);

/* === Format mapping === */
GnmDataFormat vk_ps4_vk_format_to_gnm(VkFormat format);
GnmDataFormat vk_ps4_vk_format_to_gnm_buffer(VkFormat format);
bool vk_ps4_gnm_format_is_buffer_compatible(GnmDataFormat fmt);
VkFormatProperties vk_ps4_format_properties(VkFormat format);

/* === Device extension enumeration === */
VkResult vk_ps4_enumerate_device_extensions(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties
);

/* === Stub shader (when libpsbc is not available) === */
VkResult vk_ps4_shader_compile_stub(
    const uint32_t *spirv, size_t spirv_size,
    VkShaderStageFlagBits stage,
    void **out_binary, size_t *out_binary_size
);

/* === Forward declarations of all vk_ps4_* implementation functions === */
/* These are declared here so vk_ps4_entrypoints.c can call them without
 * duplicating the forward declarations from vk_ps4_dispatch.c. */

/* Instance */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateInstance(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyInstance(VkInstance, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EnumeratePhysicalDevices(VkInstance, uint32_t *, VkPhysicalDevice *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkSampleCountFlagBits, VkImageUsageFlags, VkImageTiling, uint32_t *, VkSparseImageFormatProperties *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties *);

/* Device */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDevice(VkDevice, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue *);

/* Memory */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateMemory(VkDevice, const VkMemoryAllocateInfo *, const VkAllocationCallbacks *, VkDeviceMemory *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_MapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void **);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UnmapMemory(VkDevice, VkDeviceMemory);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FlushMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_InvalidateMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange *);

/* Buffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateBuffer(VkDevice, const VkBufferCreateInfo *, const VkAllocationCallbacks *, VkBuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetBufferMemoryRequirements2(VkDevice, const VkBufferMemoryRequirementsInfo2 *, VkMemoryRequirements2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);

/* Image */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImage(VkDevice, const VkImageCreateInfo *, const VkAllocationCallbacks *, VkImage *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImage(VkDevice, VkImage, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageMemoryRequirements2(VkDevice, const VkImageMemoryRequirementsInfo2 *, VkMemoryRequirements2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageSparseMemoryRequirements2(VkDevice, const VkImageSparseMemoryRequirementsInfo2 *, uint32_t *, VkSparseImageMemoryRequirements2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateImageView(VkDevice, const VkImageViewCreateInfo *, const VkAllocationCallbacks *, VkImageView *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateBufferView(VkDevice, const VkBufferViewCreateInfo *, const VkAllocationCallbacks *, VkBufferView *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyBufferView(VkDevice, VkBufferView, const VkAllocationCallbacks *);
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vk_ps4_GetBufferDeviceAddress(VkDevice, const VkBufferDeviceAddressInfo *);

/* Misc */
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetRenderAreaGranularity(VkDevice, VkRenderPass, VkExtent2D *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceMemoryCommitment(VkDevice, VkDeviceMemory, VkDeviceSize *);

/* Render pass / framebuffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateRenderPass(VkDevice, const VkRenderPassCreateInfo *, const VkAllocationCallbacks *, VkRenderPass *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateRenderPass2(VkDevice, const VkRenderPassCreateInfo2 *, const VkAllocationCallbacks *, VkRenderPass *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFramebuffer(VkDevice, const VkFramebufferCreateInfo *, const VkAllocationCallbacks *, VkFramebuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks *);

/* Shader / pipeline */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateShaderModule(VkDevice, const VkShaderModuleCreateInfo *, const VkAllocationCallbacks *, VkShaderModule *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyShaderModule(VkDevice, VkShaderModule, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo *, const VkAllocationCallbacks *, VkPipelineLayout *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateGraphicsPipelines(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo *, const VkAllocationCallbacks *, VkPipeline *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateComputePipelines(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo *, const VkAllocationCallbacks *, VkPipeline *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks *);

/* Pipeline cache */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreatePipelineCache(VkDevice, const VkPipelineCacheCreateInfo *, const VkAllocationCallbacks *, VkPipelineCache *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyPipelineCache(VkDevice, VkPipelineCache, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetPipelineCacheData(VkDevice, VkPipelineCache, size_t *, void *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_MergePipelineCaches(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache *);

/* Pipeline cache helpers — used by pipeline creation to skip recompilation */
void *vk_ps4_pipeline_cache_lookup(VkPipelineCache cache, uint64_t hash,
                                    uint32_t stage, size_t *out_size);
VkResult vk_ps4_pipeline_cache_insert(VkPipelineCache cache, uint64_t hash,
                                       uint32_t stage, uint32_t spirv_size,
                                       const void *binary, size_t binary_size);
uint64_t vk_ps4_pipeline_cache_hash(const void *spirv, size_t spirv_size,
                                     uint32_t stage);

/* Descriptor */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo *, const VkAllocationCallbacks *, VkDescriptorSetLayout *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo *, const VkAllocationCallbacks *, VkDescriptorPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo *, VkDescriptorSet *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_FreeDescriptorSets(VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet *, uint32_t, const VkCopyDescriptorSet *);

/* Command buffer */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateCommandPool(VkDevice, const VkCommandPoolCreateInfo *, const VkAllocationCallbacks *, VkCommandPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyCommandPool(VkDevice, VkCommandPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AllocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_FreeCommandBuffers(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EndCommandBuffer(VkCommandBuffer);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags);

/* Command buffer recording */
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetViewport(VkCommandBuffer, uint32_t, uint32_t, const VkViewport *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetScissor(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetLineWidth(VkCommandBuffer, float);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetDepthBias(VkCommandBuffer, float, float, float);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetBlendConstants(VkCommandBuffer, const float[4]);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetDepthBounds(VkCommandBuffer, float, float);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetStencilCompareMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetStencilWriteMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetStencilReference(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindVertexBuffers(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *, const VkDeviceSize *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatchIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdFillBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdUpdateBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, const void *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBlitImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageBlit *, VkFilter);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResolveImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageResolve *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyBufferToImage(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo *, VkSubpassContents);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdNextSubpass(VkCommandBuffer, VkSubpassContents);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass(VkCommandBuffer);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginRenderPass2(VkCommandBuffer, const VkRenderPassBeginInfo *, const VkSubpassBeginInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdNextSubpass2(VkCommandBuffer, const VkSubpassBeginInfo *, const VkSubpassEndInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndRenderPass2(VkCommandBuffer, const VkSubpassEndInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetEvent(VkCommandBuffer, VkEvent, VkPipelineStageFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResetEvent(VkCommandBuffer, VkEvent, VkPipelineStageFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdWaitEvents(VkCommandBuffer, uint32_t, const VkEvent *, VkPipelineStageFlags, VkPipelineStageFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearColorImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue *, uint32_t, const VkImageSubresourceRange *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment *, uint32_t, const VkClearRect *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdPushConstants(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdExecuteCommands(VkCommandBuffer, uint32_t, const VkCommandBuffer *);

/* Queue */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueSubmit(VkQueue, uint32_t, const VkSubmitInfo *, VkFence);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueueWaitIdle(VkQueue);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_DeviceWaitIdle(VkDevice);

/* Sync */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateFence(VkDevice, const VkFenceCreateInfo *, const VkAllocationCallbacks *, VkFence *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyFence(VkDevice, VkFence, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_WaitForFences(VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetFences(VkDevice, uint32_t, const VkFence *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetFenceStatus(VkDevice, VkFence);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSemaphore(VkDevice, const VkSemaphoreCreateInfo *, const VkAllocationCallbacks *, VkSemaphore *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySemaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateEvent(VkDevice, const VkEventCreateInfo *, const VkAllocationCallbacks *, VkEvent *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyEvent(VkDevice, VkEvent, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetEventStatus(VkDevice, VkEvent);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_SetEvent(VkDevice, VkEvent);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetEvent(VkDevice, VkEvent);

/* VK_KHR_timeline_semaphore */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSemaphoreCounterValueKHR(VkDevice, VkSemaphore, uint64_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_SignalSemaphoreKHR(VkDevice, const VkSemaphoreSignalInfoKHR *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_WaitSemaphoresKHR(VkDevice, const VkSemaphoreWaitInfoKHR *, uint64_t);

/* Query */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateQueryPool(VkDevice, const VkQueryPoolCreateInfo *, const VkAllocationCallbacks *, VkQueryPool *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyQueryPool(VkDevice, VkQueryPool, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void *, VkDeviceSize, VkQueryResultFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_ResetQueryPoolEXT(VkDevice, VkQueryPool, uint32_t, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdWriteTimestamp(VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdCopyQueryPoolResults(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t, VkBuffer, VkDeviceSize, VkDeviceSize, VkQueryResultFlags);

/* Swapchain */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_AcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_QueuePresentKHR(VkQueue, const VkPresentInfoKHR *);

/* Sampler */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSampler(VkDevice, const VkSamplerCreateInfo *, const VkAllocationCallbacks *, VkSampler *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySampler(VkDevice, VkSampler, const VkAllocationCallbacks *);

/* Command pool management */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetCommandPool(VkDevice, VkCommandPool, VkCommandPoolResetFlags);
VKAPI_ATTR void VKAPI_CALL vk_ps4_TrimCommandPool(VkDevice, VkCommandPool, VkCommandPoolTrimFlags);

/* Descriptor pool management */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_ResetDescriptorPool(VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags);

/* Image subresource layout */
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetImageSubresourceLayout(VkDevice, VkImage, const VkImageSubresource *, VkSubresourceLayout *);

/* === Vulkan 1.1 core functions === */
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EnumerateInstanceVersion(uint32_t *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_EnumeratePhysicalDeviceGroups(VkInstance, uint32_t *, VkPhysicalDeviceGroupProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceProperties2(VkPhysicalDevice, VkPhysicalDeviceProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFeatures2(VkPhysicalDevice, VkPhysicalDeviceFeatures2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice, VkFormat, VkFormatProperties2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2 *, VkImageFormatProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo *, VkExternalBufferProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice, const VkPhysicalDeviceExternalFenceInfo *, VkExternalFenceProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo *, VkExternalSemaphoreProperties *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2 *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2 *, uint32_t *, VkSparseImageFormatProperties2 *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateSamplerYcbcrConversion(VkDevice, const VkSamplerYcbcrConversionCreateInfo *, const VkAllocationCallbacks *, VkSamplerYcbcrConversion *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroySamplerYcbcrConversion(VkDevice, VkSamplerYcbcrConversion, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceQueue2(VkDevice, const VkDeviceQueueInfo2 *, VkQueue *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindBufferMemory2(VkDevice, uint32_t, const VkBindBufferMemoryInfo *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_BindImageMemory2(VkDevice, uint32_t, const VkBindImageMemoryInfo *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDeviceGroupPeerMemoryFeatures(VkDevice, uint32_t, uint32_t, uint32_t, VkPeerMemoryFeatureFlags *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_GetDescriptorSetLayoutSupport(VkDevice, const VkDescriptorSetLayoutCreateInfo *, VkDescriptorSetLayoutSupport *);
VKAPI_ATTR VkResult VKAPI_CALL vk_ps4_CreateDescriptorUpdateTemplate(VkDevice, const VkDescriptorUpdateTemplateCreateInfo *, const VkAllocationCallbacks *, VkDescriptorUpdateTemplate *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_DestroyDescriptorUpdateTemplate(VkDevice, VkDescriptorUpdateTemplate, const VkAllocationCallbacks *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_UpdateDescriptorSetWithTemplate(VkDevice, VkDescriptorSet, VkDescriptorUpdateTemplate, const void *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetDeviceMask(VkCommandBuffer, uint32_t);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdDispatchBase(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* VK_PS4_INTERNAL_H */
