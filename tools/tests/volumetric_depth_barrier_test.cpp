#include "rendering/vk_utils.hpp"
#include <cstdio>
#include <cstdlib>

static VkImageMemoryBarrier2 captured{};
static unsigned capturedCount = 0;
static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void VKAPI_CALL capture(VkCommandBuffer, const VkDependencyInfo* dep) {
    require(dep && dep->imageMemoryBarrierCount == 1, "one depth image transition");
    captured = dep->pImageMemoryBarriers[0];
    ++capturedCount;
}
// The production helper also contains a legacy Vulkan fallback. Resolve its
// entry point and fail if the synchronization2 callback is unexpectedly skipped.
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
    uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
    uint32_t, const VkImageMemoryBarrier*) {
    require(false, "installed synchronization2 dispatch is used");
}
int main() {
    using namespace wowee::rendering;
    setPipelineBarrier2Fn(capture);
    constexpr auto depthStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    transitionImageLayout(VK_NULL_HANDLE, VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        depthStages, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    require(capturedCount == 1 && captured.subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT,
            "sampling transition addresses depth");
    require(captured.srcAccessMask == VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT &&
            captured.dstAccessMask == VK_ACCESS_SHADER_READ_BIT,
            "opaque depth writes become visible to sunlight sampling");
    require(captured.srcStageMask == depthStages &&
            captured.dstStageMask == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            "depth-to-fragment execution dependency");
    transitionImageLayout(VK_NULL_HANDLE, VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, depthStages);
    require(capturedCount == 2 && captured.srcAccessMask == VK_ACCESS_SHADER_READ_BIT &&
            captured.dstAccessMask == (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
            "sampling finishes before depth is reused for testing or writing");
    require(captured.srcStageMask == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
            captured.dstStageMask == depthStages,
            "fragment-to-depth execution dependency");
    setPipelineBarrier2Fn(nullptr);
    std::puts("PASS: actual Vulkan helper orders opaque depth, sunlight reads, and subsequent depth reuse");
}
