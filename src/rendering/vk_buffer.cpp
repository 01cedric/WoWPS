#include "rendering/vk_buffer.hpp"
#include "rendering/vk_context.hpp"
#include "core/logger.hpp"
#include <cstring>

namespace wowee {
namespace rendering {

VkBuffer::~VkBuffer() {
    destroy();
}

VkBuffer::VkBuffer(VkBuffer&& other) noexcept
    : buf_(other.buf_), allocator_(other.allocator_), size_(other.size_) {
    other.buf_ = {};
    other.allocator_ = VK_NULL_HANDLE;
    other.size_ = 0;
}

VkBuffer& VkBuffer::operator=(VkBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        buf_ = other.buf_;
        allocator_ = other.allocator_;
        size_ = other.size_;
        other.buf_ = {};
        other.allocator_ = VK_NULL_HANDLE;
        other.size_ = 0;
    }
    return *this;
}

bool VkBuffer::uploadToGPU(VkContext& ctx, const void* data, VkDeviceSize size,
    VkBufferUsageFlags usage)
{
    destroy();
    allocator_ = ctx.getAllocator();
    size_ = size;

    buf_ = uploadBuffer(ctx, data, size, usage);
    if (!buf_.buffer) {
        LOG_ERROR("Failed to upload buffer (size=", size, ")");
        return false;
    }

    return true;
}

bool VkBuffer::createMapped(VmaAllocator allocator, VkDeviceSize size,
    VkBufferUsageFlags usage)
{
    destroy();
    allocator_ = allocator;
    size_ = size;

    buf_ = createBuffer(allocator, size, usage, VMA_MEMORY_USAGE_CPU_TO_GPU);
    if (!buf_.buffer) {
        LOG_ERROR("Failed to create mapped buffer (size=", size, ")");
        return false;
    }

    return true;
}
void VkBuffer::destroy() {
    if (buf_.buffer && allocator_) {
        destroyBuffer(allocator_, buf_);
    }
    buf_ = {};
    allocator_ = VK_NULL_HANDLE;
    size_ = 0;
}

VkDescriptorBufferInfo VkBuffer::descriptorInfo(VkDeviceSize offset, VkDeviceSize range) const {
    VkDescriptorBufferInfo info{};
    info.buffer = buf_.buffer;
    info.offset = offset;
    info.range = range;
    return info;
}

} // namespace rendering
} // namespace wowee
