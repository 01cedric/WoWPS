#include "rendering/vk_texture.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/texture_upload_layout.hpp"
#include "core/logger.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <new>
#include <vector>

namespace wowee {
namespace rendering {

namespace {
bool uploadFormat(VkFormat format, uint32_t& block, uint32_t& bytes) {
    block = 1;
    switch (format) {
    case VK_FORMAT_R8_UNORM: bytes = 1; return true;
    case VK_FORMAT_R8G8_UNORM: bytes = 2; return true;
    case VK_FORMAT_R8G8B8_UNORM: bytes = 3; return true;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB: bytes = 4; return true;
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: block = 4; bytes = 8; return true;
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK: block = 4; bytes = 16; return true;
    default: return false;
    }
}
bool mapStaging(VkContext& ctx, const AllocatedBuffer& staging, void** mapped) {
    *mapped = nullptr;
    if (!staging.buffer || !staging.allocation) return false;
    if (vmaMapMemory(ctx.getAllocator(), staging.allocation, mapped) != VK_SUCCESS)
        return false;
    if (!*mapped) {
        vmaUnmapMemory(ctx.getAllocator(), staging.allocation);
        return false;
    }
    return true;
}
} // namespace

VkTexture::~VkTexture() {
    destroy(device_, allocator_);
}

VkTexture::VkTexture(VkTexture&& other) noexcept
    : image_(other.image_), sampler_(other.sampler_), mipLevels_(other.mipLevels_),
      ownsSampler_(other.ownsSampler_), device_(other.device_),
      allocator_(other.allocator_) {
    other.image_ = {};
    other.sampler_ = VK_NULL_HANDLE;
    // Source no longer owns the sampler - ownership transferred to this instance
    other.ownsSampler_ = false;
    // ...nor the device, which is what stops its destructor freeing ours.
    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = VK_NULL_HANDLE;
}

VkTexture& VkTexture::operator=(VkTexture&& other) noexcept {
    if (this != &other) {
        // Whatever this already held is otherwise overwritten and lost.
        destroy(device_, allocator_);
        image_ = other.image_;
        sampler_ = other.sampler_;
        mipLevels_ = other.mipLevels_;
        ownsSampler_ = other.ownsSampler_;
        device_ = other.device_;
        allocator_ = other.allocator_;
        other.image_ = {};
        other.sampler_ = VK_NULL_HANDLE;
        other.ownsSampler_ = false;
        other.device_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkTexture::upload(VkContext& ctx, const uint8_t* pixels, uint32_t width, uint32_t height,
    VkFormat format, bool generateMips, std::source_location where) {
    uint32_t uploadBlock = 0, uploadBytes = 0;
    if (!pixels || !width || !height || width > 16384 || height > 16384 ||
        !uploadFormat(format, uploadBlock, uploadBytes) || uploadBlock != 1)
        return false;

#if defined(__ORBIS__) || defined(PS4) || defined(WOWEE_PS4)
    /* OpenGNM has no generic filtered blit packet for tiled images -
     * vk_ps4_CmdBlitImage silently no-ops any non-1:1 (scaling) region, so
     * the desktop generateMipmaps() path below would leave every mip past
     * level 0 uninitialized on PS4. Build the same complete mip chain on the
     * CPU instead and let the ICD tile every level through uploadMips(),
     * which goes through the same vkCmdCopyBufferToImage path already
     * confirmed to accept complete-subresource uploads. Ported from D13. */
    if (generateMips) {
        uint32_t components = 4;
        if (format == VK_FORMAT_R8_UNORM) components = 1;
        else if (format == VK_FORMAT_R8G8_UNORM) components = 2;
        else if (format == VK_FORMAT_R8G8B8_UNORM) components = 3;

        const size_t baseBytes = static_cast<size_t>(width) * height * components;

        // Level 0 is the caller's buffer, not a copy of it.
        //
        // This used to open by copying `pixels` whole into ownedLevels[0]: a
        // second full image, four megabytes for a 1024 character atlas, held
        // beside the caller's own copy for the length of the build. It is also
        // the single largest block this function asks for, so on an arena that
        // has been broken up by a session's worth of small nodes it is the
        // first request to come back null - which is the std::bad_alloc the
        // console dies of while the kernel still reports forty megabytes free.
        //
        // It bought nothing. `pixels` outlives this call in every caller (the
        // composite functions hold their working buffer to the end), and
        // uploadMips only ever reads through these pointers.
        std::vector<std::vector<uint8_t>> ownedLevels;  // levels 1..n; level 0 is `pixels`
        std::vector<const uint8_t*> levelPointers;
        std::vector<uint32_t> levelSizes;
        uint32_t levelWidth = width;
        uint32_t levelHeight = height;
        // Reserved so no push_back below can reallocate the outer vector while
        // `previous` points into one of its elements. A vector move keeps the
        // element's own buffer, so this is belt as well as braces - but the
        // rule "nothing reallocates while a pointer into it is live" is worth
        // being able to read off the code rather than deduce.
        try {
            uint32_t levels = 1;
            for (uint32_t size = std::max(width, height); size > 1; size /= 2) ++levels;
            ownedLevels.reserve(levels - 1);
        } catch (const std::bad_alloc&) {
            return false;  // a dozen pointers is not worth degrading over
        }

        // A chain that runs out of memory is finished short rather than thrown.
        //
        // Every prefix of a mip chain is a valid mip chain: the image is
        // created with exactly the number of levels handed to uploadMips and
        // the sampler's maxLod follows it, so a truncated chain costs some
        // shimmer on a character seen from across a square. That is the trade
        // this whole path is here to make. The remaining levels are a third of
        // the base between them, so in practice this only ever fires when the
        // heap is refusing kilobytes.
        const uint8_t* previous = pixels;
        try {
            while (levelWidth > 1 || levelHeight > 1) {
                const uint32_t nextWidth = std::max(1u, levelWidth / 2u);
                const uint32_t nextHeight = std::max(1u, levelHeight / 2u);
                std::vector<uint8_t> next(
                    static_cast<size_t>(nextWidth) * nextHeight * components);
                for (uint32_t y = 0; y < nextHeight; ++y) {
                    for (uint32_t x = 0; x < nextWidth; ++x) {
                        for (uint32_t c = 0; c < components; ++c) {
                            uint32_t sum = 0;
                            uint32_t samples = 0;
                            for (uint32_t oy = 0; oy < 2; ++oy) {
                                const uint32_t sy = std::min(levelHeight - 1u, y * 2u + oy);
                                for (uint32_t ox = 0; ox < 2; ++ox) {
                                    const uint32_t sx = std::min(levelWidth - 1u, x * 2u + ox);
                                    sum += previous[(static_cast<size_t>(sy) * levelWidth + sx) *
                                                    components + c];
                                    ++samples;
                                }
                            }
                            next[(static_cast<size_t>(y) * nextWidth + x) * components + c] =
                                static_cast<uint8_t>((sum + samples / 2u) / samples);
                        }
                    }
                }
                ownedLevels.push_back(std::move(next));
                previous = ownedLevels.back().data();
                levelWidth = nextWidth;
                levelHeight = nextHeight;
            }
        } catch (const std::bad_alloc&) {
            // Not LOG_*: the logger formats through std::string, and asking the
            // heap for one while unwinding an allocation failure is how a
            // diagnostic becomes the next throw.
            std::fprintf(stderr,
                         "PS4: mip chain for %ux%u stopped at %u level(s); "
                         "uploading a short chain\n",
                         width, height,
                         static_cast<unsigned>(ownedLevels.size() + 1));
        }

        try {
            levelPointers.reserve(ownedLevels.size() + 1);
            levelSizes.reserve(ownedLevels.size() + 1);
        } catch (const std::bad_alloc&) {
            return false;
        }
        levelPointers.push_back(pixels);
        levelSizes.push_back(static_cast<uint32_t>(baseBytes));
        for (const auto& level : ownedLevels) {
            levelPointers.push_back(level.data());
            levelSizes.push_back(static_cast<uint32_t>(level.size()));
        }
        LOG_DEBUG("PS4: CPU mip chain for ", width, "x", height, " texture: ",
                 levelPointers.size(), " levels");
        return uploadMips(ctx, levelPointers.data(), levelSizes.data(),
                          static_cast<uint32_t>(levelPointers.size()),
                          width, height, format);
    }
#endif

    mipLevels_ = generateMips
        ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1
        : 1;

    // Determine bytes per pixel from format
    uint32_t bpp = 4; // default RGBA8
    if (format == VK_FORMAT_R8_UNORM) bpp = 1;
    else if (format == VK_FORMAT_R8G8_UNORM) bpp = 2;
    else if (format == VK_FORMAT_R8G8B8_UNORM) bpp = 3;

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * bpp;

    // A texture with one level and host image copy needs no staging buffer,
    // no queue submission and no barriers: the pixels go into the image and
    // the layout moves on the host. Mipped textures still take the staging
    // path, because their levels are built by a GPU blit chain that has to
    // read back from the image anyway.
    const bool useHostCopy = ctx.isHostImageCopySupported() && !generateMips;

    AllocatedBuffer staging{};
    if (!useHostCopy) {
        staging = createBuffer(ctx.getAllocator(), imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

        void* mapped = nullptr;
        if (!mapStaging(ctx, staging, &mapped)) {
            LOG_ERROR("Texture upload: staging allocation/map failed");
            destroyBuffer(ctx.getAllocator(), staging);
            return false;
        }
        std::memcpy(mapped, pixels, imageSize);
        const VkResult flushed = vmaFlushAllocation(ctx.getAllocator(), staging.allocation, 0, imageSize);
        vmaUnmapMemory(ctx.getAllocator(), staging.allocation);
        if (flushed != VK_SUCCESS) {
            destroyBuffer(ctx.getAllocator(), staging);
            return false;
        }
    }

    // Create image with transfer dst + src (src for mipmap generation) + sampled
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (generateMips) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (useHostCopy) {
        usage |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT;
    }
    // Release anything this object already held: a second upload over the
    // same texture used to abandon the first, which is how three quest
    // marker textures survived every re-initialisation.
    destroy(device_, allocator_);
    device_ = ctx.getDevice();
    allocator_ = ctx.getAllocator();
    image_ = createImage(ctx.getDevice(), ctx.getAllocator(), width, height,
        format, usage, VK_SAMPLE_COUNT_1_BIT, mipLevels_, where);

    if (!image_.image || !image_.imageView) {
        if (!useHostCopy) destroyBuffer(ctx.getAllocator(), staging);
        destroy(device_, allocator_);
        return false;
    }

    if (useHostCopy) {
        VkHostImageLayoutTransitionInfoEXT toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT;
        toDst.image = image_.image;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0,
                                  .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
        if (ctx.transitionImageLayoutHostFn()(ctx.getDevice(), 1, &toDst) != VK_SUCCESS) {
            destroy(device_, allocator_);
            return false;
        }

        VkMemoryToImageCopyEXT region{};
        region.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT;
        region.pHostPointer = pixels;
        region.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0,
                                   .baseArrayLayer = 0, .layerCount = 1};
        region.imageExtent = {.width = width, .height = height, .depth = 1};

        VkCopyMemoryToImageInfoEXT copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT;
        copy.dstImage = image_.image;
        copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy.regionCount = 1;
        copy.pRegions = &region;
        if (ctx.copyMemoryToImageFn()(ctx.getDevice(), &copy) != VK_SUCCESS) {
            LOG_ERROR("host image copy failed for a ", width, "x", height, " texture");
            destroy(device_, allocator_);
            return false;
        }

        VkHostImageLayoutTransitionInfoEXT toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (ctx.transitionImageLayoutHostFn()(ctx.getDevice(), 1, &toRead) != VK_SUCCESS) {
            destroy(device_, allocator_);
            return false;
        }
        return true;
    }

    const bool submitted = ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        // Transition to transfer dst
        transitionImageLayout(cmd, image_.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        // Copy staging buffer to image (mip 0)
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {.width = width, .height = height, .depth = 1};

        vkCmdCopyBufferToImage(cmd, staging.buffer, image_.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        if (!generateMips) {
            // Transition to shader read
            transitionImageLayout(cmd, image_.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    });

    if (!submitted) {
        destroyBuffer(ctx.getAllocator(), staging);
        destroy(device_, allocator_);
        return false;
    }
    if (generateMips) {
        generateMipmaps(ctx, format, width, height);
    }

    if (ctx.isInUploadBatch()) {
        ctx.deferStagingCleanup(staging);
    } else {
        destroyBuffer(ctx.getAllocator(), staging);
    }
    return true;
}

bool VkTexture::uploadMips(VkContext& ctx, const uint8_t* const* mipData,
    const uint32_t* mipSizes, uint32_t mipCount, uint32_t width, uint32_t height, VkFormat format)
{
    uint32_t block = 0, blockBytes = 0;
    std::vector<TextureUploadLevel> levels;
    uint64_t totalSize = 0;
    if (!uploadFormat(format, block, blockBytes) ||
        !textureUploadLayout(width, height, block, block, blockBytes,
                             mipData, mipSizes, mipCount, levels, totalSize)) {
        LOG_ERROR("Texture upload: invalid dimensions or truncated mip chain");
        return false;
    }
    mipLevels_ = mipCount;
    AllocatedBuffer staging = createBuffer(ctx.getAllocator(), totalSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* mapped = nullptr;
    if (!mapStaging(ctx, staging, &mapped)) {
        LOG_ERROR("Texture mip upload: staging allocation/map failed");
        destroyBuffer(ctx.getAllocator(), staging);
        return false;
    }
    for (uint32_t i = 0; i < mipCount; i++)
        std::memcpy(static_cast<uint8_t*>(mapped) + levels[i].offset,
                    mipData[i], levels[i].bytes);
    const VkResult flushed = vmaFlushAllocation(ctx.getAllocator(), staging.allocation, 0, totalSize);
    vmaUnmapMemory(ctx.getAllocator(), staging.allocation);
    if (flushed != VK_SUCCESS) {
        destroyBuffer(ctx.getAllocator(), staging);
        return false;
    }

    // Release anything this object already held: a second upload over the
    // same texture used to abandon the first, which is how three quest
    // marker textures survived every re-initialisation.
    destroy(device_, allocator_);
    device_ = ctx.getDevice();
    allocator_ = ctx.getAllocator();
    image_ = createImage(ctx.getDevice(), ctx.getAllocator(), width, height,
        format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SAMPLE_COUNT_1_BIT, mipLevels_);

    if (!image_.image || !image_.imageView) {
        destroyBuffer(ctx.getAllocator(), staging);
        destroy(device_, allocator_);
        return false;
    }

    const bool submitted = ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, image_.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        uint32_t mipW = width, mipH = height;
        for (uint32_t i = 0; i < mipCount; i++) {
            VkBufferImageCopy region{};
            region.bufferOffset = levels[i].offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = i;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {.width = mipW, .height = mipH, .depth = 1};

            vkCmdCopyBufferToImage(cmd, staging.buffer, image_.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            mipW = std::max(1u, mipW / 2);
            mipH = std::max(1u, mipH / 2);
        }

        transitionImageLayout(cmd, image_.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });

    if (!submitted) {
        destroyBuffer(ctx.getAllocator(), staging);
        destroy(device_, allocator_);
        return false;
    }
    if (ctx.isInUploadBatch()) {
        ctx.deferStagingCleanup(staging);
    } else {
        destroyBuffer(ctx.getAllocator(), staging);
    }
#if defined(__ORBIS__) || defined(PS4) || defined(WOWEE_PS4)
    {
        static uint32_t upload_mips_log_count = 0;
        if (upload_mips_log_count < 24) {
            upload_mips_log_count++;
            LOG_INFO("PS4: uploadMips OK ", width, "x", height, " mips=", mipCount,
                     " image=", (void*)image_.image);
        }
    }
#endif
    return true;
}

namespace {
// Relaxed because nothing orders on these; they are only ever read back once,
// after the uploads that write them have finished.
std::atomic<uint64_t> g_blockUploadTextures{0};
std::atomic<uint64_t> g_blockUploadBytes{0};
std::atomic<uint64_t> g_blockUploadDecodedBytes{0};
} // namespace

VkTexture::BlockUploadTally VkTexture::blockUploadTally() {
    return {.textures = g_blockUploadTextures.load(std::memory_order_relaxed),
            .blockBytes = g_blockUploadBytes.load(std::memory_order_relaxed),
            .decodedBytes = g_blockUploadDecodedBytes.load(std::memory_order_relaxed)};
}

bool VkTexture::uploadBLP(VkContext& ctx, const pipeline::BLPImage& image) {
    if (!image.isValid()) return false;

    if (!image.isBlockCompressed()) {
        return upload(ctx, image.data.data(),
                      static_cast<uint32_t>(image.width),
                      static_cast<uint32_t>(image.height),
                      VK_FORMAT_R8G8B8A8_UNORM, true);
    }

    if (!ctx.isBlockCompressionSupported()) {
        // Keep queued tiles compact; hold only this texture's decoded base
        // while upload() copies it into GPU-owned staging and builds mips.
        // No BC commands are recorded into an upload batch on this path.
        const auto decoded = pipeline::BLPLoader::decodeBaseLevel(image);
        if (decoded.empty()) return false;
        return upload(ctx, decoded.data(), static_cast<uint32_t>(image.width),
                      static_cast<uint32_t>(image.height), VK_FORMAT_R8G8B8A8_UNORM, true);
    }

    VkFormat format = VK_FORMAT_UNDEFINED;
    switch (image.compression) {
        case pipeline::BLPCompression::DXT1: format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
        case pipeline::BLPCompression::DXT3: format = VK_FORMAT_BC2_UNORM_BLOCK; break;
        case pipeline::BLPCompression::DXT5: format = VK_FORMAT_BC3_UNORM_BLOCK; break;
        default: return false;
    }

    // A device that cannot sample the block format has to be told before the
    // image is created, not after: the caller reloads the file decoded.
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(ctx.getPhysicalDevice(), format, &props);
    if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        return false;
    }

    std::vector<const uint8_t*> levels;
    std::vector<uint32_t> sizes;
    levels.reserve(image.mipmaps.size());
    sizes.reserve(image.mipmaps.size());
    for (const auto& level : image.mipmaps) {
        levels.push_back(level.data());
        sizes.push_back(static_cast<uint32_t>(level.size()));
    }

    if (!uploadMips(ctx, levels.data(), sizes.data(),
                    static_cast<uint32_t>(levels.size()),
                    static_cast<uint32_t>(image.width),
                    static_cast<uint32_t>(image.height), format)) {
        return false;
    }

    // Counted after the upload succeeds, so a texture the device refused above
    // is not credited with a saving it never made.
    g_blockUploadTextures.fetch_add(1, std::memory_order_relaxed);
    g_blockUploadBytes.fetch_add(image.approxUploadBytes(), std::memory_order_relaxed);
    g_blockUploadDecodedBytes.fetch_add(image.approxDecodedUploadBytes(),
                                        std::memory_order_relaxed);
    return true;
}

bool VkTexture::createDepth(VkContext& ctx, uint32_t width, uint32_t height, VkFormat format) {
    mipLevels_ = 1;

    // Release anything this object already held: a second upload over the
    // same texture used to abandon the first, which is how three quest
    // marker textures survived every re-initialisation.
    destroy(device_, allocator_);
    device_ = ctx.getDevice();
    allocator_ = ctx.getAllocator();
    image_ = createImage(ctx.getDevice(), ctx.getAllocator(), width, height,
        format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    if (!image_.image || !image_.imageView) { destroy(device_, allocator_); return false; }

    const bool submitted = ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, image_.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    });

    if (!submitted) { destroy(device_, allocator_); return false; }
    return true;
}

// Shared sampler finalization: try the global cache first (avoids duplicate Vulkan
// sampler objects), fall back to direct creation if no VkContext is available.
bool VkTexture::finalizeSampler(VkDevice device, const VkSamplerCreateInfo& samplerInfo) {
    // A texture may be given a sampler before an image; record the device so
    // the destructor can still free it.
    if (device_ == VK_NULL_HANDLE) device_ = device;
    auto* ctx = VkContext::globalInstance();
    if (ctx) {
        sampler_ = ctx->getOrCreateSampler(samplerInfo);
        ownsSampler_ = false;
        return sampler_ != VK_NULL_HANDLE;
    }
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        LOG_ERROR("Failed to create texture sampler");
        return false;
    }
    ownsSampler_ = true;
    return true;
}

bool VkTexture::createSampler(VkDevice device,
    VkFilter minFilter, VkFilter magFilter,
    VkSamplerAddressMode addressMode, float maxAnisotropy)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.minFilter = minFilter;
    samplerInfo.magFilter = magFilter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = maxAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = (minFilter == VK_FILTER_LINEAR)
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels_ > 0 ? mipLevels_ - 1 : 0);
    return finalizeSampler(device, samplerInfo);
}

bool VkTexture::createSampler(VkDevice device,
    VkFilter filter,
    VkSamplerAddressMode addressModeU,
    VkSamplerAddressMode addressModeV,
    float maxAnisotropy,
    float mipLodBias)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.minFilter = filter;
    samplerInfo.magFilter = filter;
    samplerInfo.addressModeU = addressModeU;
    samplerInfo.addressModeV = addressModeV;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = maxAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = (filter == VK_FILTER_LINEAR)
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = mipLodBias;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels_ > 0 ? mipLevels_ - 1 : 0);
    return finalizeSampler(device, samplerInfo);
}
void VkTexture::destroy(VkDevice device, VmaAllocator allocator) {
    // Nothing was ever created, or this has already run. Both are ordinary:
    // the destructor calls this after an explicit destroy() has, and a
    // default-constructed texture is destroyed without having been used.
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (sampler_ != VK_NULL_HANDLE && ownsSampler_) {
        vkDestroySampler(device, sampler_, nullptr);
    }
    sampler_ = VK_NULL_HANDLE;
    ownsSampler_ = false;
    destroyImage(device, allocator, image_);
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

VkDescriptorImageInfo VkTexture::descriptorInfo(VkImageLayout layout) const {
    VkDescriptorImageInfo info{};
    info.sampler = sampler_;
    info.imageView = image_.imageView;
    info.imageLayout = layout;
    return info;
}

void VkTexture::generateMipmaps(VkContext& ctx, VkFormat format,
    uint32_t width, uint32_t height)
{
    // Check if format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(ctx.getPhysicalDevice(), format, &formatProperties);

    bool canBlit = (formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;

    if (!canBlit) {
        LOG_WARNING("Format does not support linear blitting for mipmap generation");
        // Fall back to simple transition
        ctx.immediateSubmit([&](VkCommandBuffer cmd) {
            transitionImageLayout(cmd, image_.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        });
        return;
    }

    ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        int32_t mipW = static_cast<int32_t>(width);
        int32_t mipH = static_cast<int32_t>(height);

        for (uint32_t i = 1; i < mipLevels_; i++) {
            // Transition previous mip to transfer src
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier.image = image_.image;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            VkDependencyInfo barrierDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            barrierDep.dependencyFlags = 0;
            barrierDep.imageMemoryBarrierCount = 1;
            barrierDep.pImageMemoryBarriers = &barrier;
            cmdPipelineBarrier2(cmd, barrierDep);

            // Blit from previous mip to current
            VkImageBlit blit{};
            blit.srcOffsets[0] = {.x = 0, .y = 0, .z = 0};
            blit.srcOffsets[1] = {.x = mipW, .y = mipH, .z = 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {.x = 0, .y = 0, .z = 0};
            blit.dstOffsets[1] = {
                .x = mipW > 1 ? mipW / 2 : 1,
                .y = mipH > 1 ? mipH / 2 : 1,
                .z = 1
            };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(cmd,
                image_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            // Transition previous mip to shader read
            barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            VkDependencyInfo toReadDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            toReadDep.imageMemoryBarrierCount = 1;
            toReadDep.pImageMemoryBarriers = &barrier;
            cmdPipelineBarrier2(cmd, toReadDep);

            mipW = mipW > 1 ? mipW / 2 : 1;
            mipH = mipH > 1 ? mipH / 2 : 1;
        }

        // Transition last mip to shader read
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.image = image_.image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mipLevels_ - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkDependencyInfo barrierDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        barrierDep.dependencyFlags = 0;
        barrierDep.imageMemoryBarrierCount = 1;
        barrierDep.pImageMemoryBarriers = &barrier;
        cmdPipelineBarrier2(cmd, barrierDep);
    });
}

} // namespace rendering
} // namespace wowee
