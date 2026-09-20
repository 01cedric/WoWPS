// Included by post_process_pipeline.cpp. World-space, shadowed single scattering;
// no radial screen blur, history buffers, compute dispatch, or extra shadow pass.
namespace {
struct alignas(16) VolumetricUniforms {
    glm::mat4 inverseRelativeViewProjection;
    glm::mat4 lightMatrix;
    glm::vec4 camera;
    glm::vec4 sunDirection;
    glm::vec4 sunColor;
    glm::vec4 parameters; // max distance, density, sample count, scattering albedo
    glm::mat4 nearLightMatrix;
    glm::vec4 fogParameters; // extinction, base height, height falloff, maximum distance
    glm::vec4 fogColor;
};
static_assert(sizeof(VolumetricUniforms) == 288);
static_assert(offsetof(VolumetricUniforms, fogParameters) == 256);
static_assert(offsetof(VolumetricUniforms, fogColor) == 272);

VkPipelineColorBlendAttachmentState volumetricCompositeBlend(bool fog = false) {
    VkPipelineColorBlendAttachmentState state{};
    state.blendEnable = VK_TRUE;
    state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    state.dstColorBlendFactor = fog ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    // Preserve destination alpha; only the RGB atmosphere is composited.
    state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    state.alphaBlendOp = VK_BLEND_OP_ADD;
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    return state;
}
}

void PostProcessPipeline::setVolumetricIntensity(float intensity) {
    volumetric_.intensity = clampVolumetricIntensity(intensity);
}

void PostProcessPipeline::setVolumetricFogIntensity(float intensity) {
    volumetric_.fogIntensity = clampVolumetricFogIntensity(intensity);
}

void PostProcessPipeline::setVolumetricFogEnvironment(const glm::vec3& color, float groundHeight, float deltaTime) {
    auto& v = volumetric_;
    v.fogColor = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    if (!std::isfinite(groundHeight)) return;
    // Smooth terrain following; teleport/character transitions reset immediately.
    if (!v.fogAnchorInitialized || std::abs(groundHeight-v.fogGroundHeight) > 40.0f) {
        v.fogGroundHeight = groundHeight;
        v.fogAnchorInitialized = true;
    } else {
        const float blend = 1.0f-std::exp(-std::clamp(deltaTime,0.0f,0.25f)*0.5f);
        v.fogGroundHeight += (groundHeight-v.fogGroundHeight)*blend;
    }
}

void PostProcessPipeline::setVolumetricDebug(int mode) {
    volumetric_.debug = std::clamp(mode,0,4);
}

void PostProcessPipeline::setVolumetricQuality(int quality) {
    quality = std::clamp(quality, 0, 2);
    if (volumetric_.quality == quality) return;
    volumetric_.quality = quality;
    volumetric_.needsRecreate = true;
    volumetric_.failed = false;
}

void PostProcessPipeline::setVolumetricLighting(const glm::mat4& matrix, const glm::mat4& nearMatrix,
    const glm::vec3& travel, const glm::vec3& color, VkImageView shadow, bool allowed,
    const char* blockedReason) {
    volumetric_.lightMatrix = matrix;
    volumetric_.nearLightMatrix = nearMatrix;
    volumetric_.lightTravel = travel;
    volumetric_.lightColor = glm::max(color, glm::vec3(0.0f));
    volumetric_.shadowView = shadow;
    volumetric_.raysAllowed = allowed && shadow && glm::dot(travel, travel) > 0.01f;
    const bool fogAllowed = volumetric_.fogEnabled && volumetric_.fogIntensity > 0.0f &&
        std::strcmp(blockedReason,"camera-underwater") != 0 && std::strcmp(blockedReason,"surface-shadow-inspection") != 0;
    volumetric_.allowed = (volumetric_.raysEnabled && volumetric_.raysAllowed) || fogAllowed;
    volumetric_.blockedReason = !allowed ? blockedReason : !shadow ? "no-shadow-view" :
        glm::dot(travel, travel) <= 0.01f ? "invalid-light-direction" : "ready";
    // Per-instance bounded diagnostics remain available when the gate blocks
    // rendering. Log the first update and every 300 thereafter, never each frame.
    if (volumetric_.lightingDiagnosticsFrames++ % 300u == 0u) {
        LOG_INFO("[VOLUME_LIGHT] allowed=", volumetric_.allowed,
                 " reason=", volumetric_.blockedReason,
                 " quality=", volumetric_.quality,
                 " intensity=", volumetric_.intensity,
                 " raysEnabled=", volumetric_.raysEnabled,
                 " raysAllowed=", volumetric_.raysAllowed,
                 " fogEnabled=", volumetric_.fogEnabled,
                 " fogIntensity=", volumetric_.fogIntensity,
                 " fogBaseHeight=", volumetric_.fogGroundHeight,
                 " exposure=", kVolumetricDisplayExposure,
                 " effectiveGain=", effectiveVolumetricIntensity(volumetric_.intensity),
                 " sourceRGB=", volumetric_.lightColor.r, ",", volumetric_.lightColor.g, ",", volumetric_.lightColor.b,
                 " travel=", travel.x, ",", travel.y, ",", travel.z,
                 " shadowView=", shadow != VK_NULL_HANDLE);
    }
}

bool PostProcessPipeline::initVolumetricResources() {
    const auto device = vkCtx_->getDevice();
    const auto allocator = vkCtx_->getAllocator();
    auto& v = volumetric_;
    v.resolutionFactor = v.quality == 1 ? 4u : 2u;
    const uint32_t factor = v.resolutionFactor;
    v.extent = {std::max(1u, (fxaa_.sceneExtent.width + factor-1) / factor),
                std::max(1u, (fxaa_.sceneExtent.height + factor-1) / factor)};
    // Restore the established RGBA16F attachment path after hardware banding
    // with R16F. Scalar peak processing still uses red; other channels are unused.
    // Depth gates read FP32 scene depth. Low: 0.88 MiB; High: 3.52 MiB
    // across both low-resolution frame slots at 720p.
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1; rp.pAttachments = &attachment;
    rp.subpassCount = 1; rp.pSubpasses = &subpass;
    rp.dependencyCount = 1; rp.pDependencies = &dependency;
    if (vkCreateRenderPass(device, &rp, nullptr, &v.renderPass) != VK_SUCCESS) return false;

    // Reuse scene color as a load-only blend destination at scene resolution.
    // Resolve/denoise run at scene resolution, never repeated during output upscale.
    VkAttachmentDescription compositeAttachment = attachment;
    compositeAttachment.format = vkCtx_->getSwapchainFormat();
    compositeAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    compositeAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderPassCreateInfo compositeRp = rp;
    compositeRp.pAttachments = &compositeAttachment;
    if (vkCreateRenderPass(device,&compositeRp,nullptr,&v.compositeRenderPass) != VK_SUCCESS) return false;
    VkFramebufferCreateInfo compositeFb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    compositeFb.renderPass = v.compositeRenderPass;
    compositeFb.attachmentCount = 1;
    compositeFb.pAttachments = &fxaa_.sceneColor.imageView;
    compositeFb.width = fxaa_.sceneExtent.width;
    compositeFb.height = fxaa_.sceneExtent.height;
    compositeFb.layers = 1;
    if (vkCreateFramebuffer(device,&compositeFb,nullptr,&v.compositeFramebuffer) != VK_SUCCESS) return false;

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    // Raw depth comparison in GLSL avoids immutable comparison samplers, which
    // the console backend deliberately does not implement.
    v.sampler = vkCtx_->getOrCreateSampler(sampler);
    if (!v.sampler) return false;
    // Only radiance uses linear filtering. Depth and shadow visibility keep
    // their nearest sampler so paired denoise taps cannot soften occluders.
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    v.radianceSampler = vkCtx_->getOrCreateSampler(sampler);
    if (!v.radianceSampler) return false;
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount = 4; sl.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &sl, nullptr, &v.setLayout) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &v.setLayout;
    if (vkCreatePipelineLayout(device, &pl, nullptr, &v.pipelineLayout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 18},
                                 {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 6; pool.poolSizeCount = 2; pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &pool, nullptr, &v.pool) != VK_SUCCESS) return false;
    VkDescriptorSetLayout layouts[6]{v.setLayout, v.setLayout, v.setLayout, v.setLayout, v.setLayout, v.setLayout};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = v.pool; alloc.descriptorSetCount = 6; alloc.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device, &alloc, v.sets) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < v.frames; ++i) {
        v.scattering[i] = createImage(device, allocator, v.extent.width, v.extent.height,
            attachment.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        // Store every reconstruction result, including exact edge fallback rays,
        // before denoising. Two 720p RGBA16F targets use 14.06 MiB total.
        v.resolvedScattering[i] = createImage(device, allocator,
            fxaa_.sceneExtent.width, fxaa_.sceneExtent.height, attachment.format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        v.uniforms[i] = createBuffer(allocator, sizeof(VolumetricUniforms),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        if (!v.scattering[i].imageView || !v.resolvedScattering[i].imageView || !v.uniforms[i].info.pMappedData) return false;
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = v.renderPass; fb.attachmentCount = 1; fb.pAttachments = &v.scattering[i].imageView;
        fb.width = v.extent.width; fb.height = v.extent.height; fb.layers = 1;
        if (vkCreateFramebuffer(device, &fb, nullptr, &v.framebuffer[i]) != VK_SUCCESS) return false;
        fb.pAttachments = &v.resolvedScattering[i].imageView;
        fb.width = fxaa_.sceneExtent.width; fb.height = fxaa_.sceneExtent.height;
        if (vkCreateFramebuffer(device, &fb, nullptr, &v.resolveFramebuffer[i]) != VK_SUCCESS) return false;
    }
    VkShaderModule vertex, march, resolve, composite;
    if (!vertex.loadFromFile(device, "assets/shaders/postprocess.vert.spv") ||
        !march.loadFromFile(device, "assets/shaders/volumetric.frag.spv") ||
        !resolve.loadFromFile(device, "assets/shaders/volumetric_resolve.frag.spv") ||
        !composite.loadFromFile(device, "assets/shaders/volumetric_composite.frag.spv")) {
        vertex.destroy(); march.destroy(); resolve.destroy(); composite.destroy(); return false;
    }
    auto build = [&](VkShaderModule& fragment, VkRenderPass pass, bool compositePass, bool fog = false) {
        return PipelineBuilder().setShaders(vertex.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                fragment.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
            .setVertexInput({}, {}).setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE).setNoDepthTest()
            .setColorBlendAttachment(compositePass ? volumetricCompositeBlend(fog) : PipelineBuilder::blendDisabled())
            .setMultisample(VK_SAMPLE_COUNT_1_BIT).setLayout(v.pipelineLayout)
            .setRenderPass(pass).setDynamicStates(viewportAndScissorDynamic())
            .build(device, vkCtx_->getPipelineCache());
    };
    v.pipeline = build(march, v.renderPass, false);
    v.resolvePipeline = build(resolve, v.renderPass, false);
    v.compositePipeline = build(composite, v.compositeRenderPass, true);
    v.debugPipeline = build(composite, v.compositeRenderPass, false);
    v.fogCompositePipeline = build(composite, v.compositeRenderPass, true, true);
    vertex.destroy(); march.destroy(); resolve.destroy(); composite.destroy();
    if (!v.pipeline || !v.resolvePipeline || !v.compositePipeline || !v.debugPipeline || !v.fogCompositePipeline) return false;
    LOG_INFO("Volumetric sunlight: ", v.extent.width, "x", v.extent.height,
             "; Low quarter / High half resolution; 8/12 march, 32/48 exact edge samples; full-resolution depth-aware denoise; 70% HG0.65 + 30% Rayleigh, 10x sourceward calibration; analytic height fog; bounded LDR blend");
    LOG_INFO("[VOLUME_DENOISE] mode=fullres-depth-aware storage=RGBA16F-peak passes=3 quality=", v.quality,
             " lowres=", v.extent.width, "x", v.extent.height,
             " resolve=", fxaa_.sceneExtent.width, "x", fxaa_.sceneExtent.height,
             " composite=", fxaa_.sceneExtent.width, "x", fxaa_.sceneExtent.height,
             " fallbackSamples=", v.quality == 1 ? 32 : 48,
             " fallbackFiltered=1 stratumCoverage=coherent-texel frameSlots=", v.frames);
    v.needsRecreate = false;
    return true;
}

void PostProcessPipeline::destroyVolumetricResources() {
    if (!vkCtx_) return;
    const auto device = vkCtx_->getDevice();
    const auto allocator = vkCtx_->getAllocator();
    auto& v = volumetric_;
    destroy(device, v.pipeline); destroy(device, v.resolvePipeline); destroy(device, v.compositePipeline); destroy(device, v.debugPipeline); destroy(device, v.fogCompositePipeline);
    destroy(device, v.pipelineLayout); destroy(device, v.pool); destroy(device, v.setLayout);
    for (uint32_t i = 0; i < v.frames; ++i) {
        if (v.framebuffer[i]) vkDestroyFramebuffer(device, v.framebuffer[i], nullptr);
        v.framebuffer[i] = VK_NULL_HANDLE;
        if (v.resolveFramebuffer[i]) vkDestroyFramebuffer(device, v.resolveFramebuffer[i], nullptr);
        v.resolveFramebuffer[i] = VK_NULL_HANDLE;
        destroyImage(device, allocator, v.scattering[i]);
        destroyImage(device, allocator, v.resolvedScattering[i]);
        destroyBuffer(allocator, v.uniforms[i]);
    }
    // This framebuffer aliases the FXAA scene image and must retire first.
    if (v.compositeFramebuffer) vkDestroyFramebuffer(device,v.compositeFramebuffer,nullptr);
    v.compositeFramebuffer = VK_NULL_HANDLE;
    if (v.compositeRenderPass) vkDestroyRenderPass(device,v.compositeRenderPass,nullptr);
    v.compositeRenderPass = VK_NULL_HANDLE;
    if (v.renderPass) vkDestroyRenderPass(device, v.renderPass, nullptr);
    v.renderPass = VK_NULL_HANDLE; v.sampler = VK_NULL_HANDLE; v.radianceSampler = VK_NULL_HANDLE;
    v.rendered = false;
    for (auto& set : v.sets) set = VK_NULL_HANDLE;
}

void PostProcessPipeline::renderVolumetricScattering() {
    auto& v = volumetric_;
    if (!v.pipeline || !v.allowed || !worldRendering_ || !camera_ || v.quality == 0 ||
        fsr2_.enabled || vkCtx_->getMsaaSamples() != VK_SAMPLE_COUNT_1_BIT) return;
    const bool fogActive = v.fogEnabled && v.fogIntensity > 0.0f;
    const bool raysActive = v.raysEnabled && v.raysAllowed && v.intensity > 0.0f;
    if (!fogActive && (!raysActive || glm::dot(v.lightColor, v.lightColor) <= 1.0e-10f)) return;
    const uint32_t fi = vkCtx_->getCurrentFrame();
    VolumetricUniforms data{};
    // Reconstruct eye-relative vectors. Inverting a translated world VP and
    // subtracting a large world camera position loses precision before the
    // 160-yard march even starts, shifting samples across tree/building edges.
    const glm::mat4 viewRotation = glm::mat4(glm::mat3(camera_->getViewMatrix()));
    data.inverseRelativeViewProjection = glm::transpose(viewRotation) * glm::inverse(camera_->getProjectionMatrix());
    data.lightMatrix = v.lightMatrix;
    data.nearLightMatrix = v.nearLightMatrix;
    data.camera = glm::vec4(camera_->getPosition(), 1.0f);
    data.sunDirection = glm::vec4(glm::dot(v.lightTravel,v.lightTravel)>0.01f ? glm::normalize(-v.lightTravel) : glm::vec3(0,0,1), float(v.resolutionFactor));
    data.sunColor = glm::vec4(v.lightColor * (raysActive ? effectiveVolumetricIntensity(v.intensity) : 0.0f), float(v.debug));
    // Explicit display exposure affects only shadowed scattering, not surface
    // lighting, halo size, density or shadow visibility. Albedo stays bounded.
    // Keep the original moon/day authored-color ratio and fixed sample budget.
    data.parameters = glm::vec4(160.0f, kVolumetricExtinction, v.quality == 1 ? 8.0f : 12.0f, 0.9f);
    data.fogParameters = glm::vec4(fogActive ? 0.006f*v.fogIntensity : 0.0f, v.fogGroundHeight, 0.10f, 160.0f);
    data.fogColor = glm::vec4(v.fogColor, 0.0f);
    std::memcpy(v.uniforms[fi].info.pMappedData, &data, sizeof(data));
    vmaFlushAllocation(vkCtx_->getAllocator(), v.uniforms[fi].allocation, 0, sizeof(data));
    // Fog-only/blocked-source frames need only the analytic composite. Neither
    // the reduced march nor the full-resolution reconstruction contributes any
    // radiance. Avoid both draws and their attachment bandwidth entirely.
    const bool hasScattering = glm::dot(glm::vec3(data.sunColor), glm::vec3(data.sunColor)) > 0.0f;
    // Every descriptor and UBO belongs to the frame whose fence beginFrame has
    // waited for. Other frames' shadow views and uniforms are never rewritten.
    VkDescriptorBufferInfo buffer{v.uniforms[fi].buffer, 0, sizeof(data)};
    const VkImageView safeShadow = v.raysAllowed ? v.shadowView : fxaa_.sceneDepth.imageView;
    const VkImageLayout safeShadowLayout = v.raysAllowed ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    for (uint32_t pass = hasScattering ? 0u : 2u; pass < 3; ++pass) {
        VkDescriptorImageInfo images[3]{
            {v.sampler, fxaa_.sceneDepth.imageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            // Bind a valid, already sampled image even when the shader's zero
            // light branch never reads scattering. Do not describe an unwritten
            // transient color target in SHADER_READ layout on its first frame.
            {hasScattering && pass == 2 ? v.radianceSampler : v.sampler, !hasScattering ? safeShadow : pass == 2 ? v.resolvedScattering[fi].imageView : pass == 1 ? v.scattering[fi].imageView : safeShadow,
             !hasScattering ? safeShadowLayout : pass ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : safeShadowLayout},
            {v.sampler, safeShadow, safeShadowLayout}};
        VkWriteDescriptorSet writes[4]{};
        for (uint32_t b = 0; b < 4; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = v.sets[fi * 3 + pass]; writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = b == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (b == 2) writes[b].pBufferInfo = &buffer; else writes[b].pImageInfo = &images[b == 3 ? 2 : b];
        }
        vkUpdateDescriptorSets(vkCtx_->getDevice(), 4, writes, 0, nullptr);
    }
    transitionImageLayout(currentCmd_, fxaa_.sceneDepth.image,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (!hasScattering) {
        v.rendered = true; // Composite and finishVolumetricFrame still execute.
        return;
    }
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = v.renderPass; begin.framebuffer = v.framebuffer[fi]; begin.renderArea.extent = v.extent;
    vkCmdBeginRenderPass(currentCmd_, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{}; viewport.width = float(v.extent.width); viewport.height = float(v.extent.height); viewport.maxDepth = 1.0f;
    VkRect2D scissor{}; scissor.extent = v.extent;
    vkCmdSetViewport(currentCmd_, 0, 1, &viewport); vkCmdSetScissor(currentCmd_, 0, 1, &scissor);
    vkCmdBindPipeline(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, v.pipeline);
    vkCmdBindDescriptorSets(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, v.pipelineLayout,
        0, 1, &v.sets[fi * 3], 0, nullptr);
    vkCmdDraw(currentCmd_, 3, 1, 0, 0);
    vkCmdEndRenderPass(currentCmd_);
    // The outgoing render-pass dependency makes low-resolution writes visible
    // to reconstruction; the same dependency exposes resolve writes to denoise.
    begin.framebuffer = v.resolveFramebuffer[fi];
    begin.renderArea.extent = fxaa_.sceneExtent;
    vkCmdBeginRenderPass(currentCmd_, &begin, VK_SUBPASS_CONTENTS_INLINE);
    viewport.width = float(fxaa_.sceneExtent.width);
    viewport.height = float(fxaa_.sceneExtent.height);
    scissor.extent = fxaa_.sceneExtent;
    vkCmdSetViewport(currentCmd_, 0, 1, &viewport);
    vkCmdSetScissor(currentCmd_, 0, 1, &scissor);
    vkCmdBindPipeline(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, v.resolvePipeline);
    vkCmdBindDescriptorSets(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, v.pipelineLayout,
        0, 1, &v.sets[fi * 3 + 1], 0, nullptr);
    vkCmdDraw(currentCmd_, 3, 1, 0, 0);
    vkCmdEndRenderPass(currentCmd_);
    v.rendered = true;
}

void PostProcessPipeline::compositeVolumetricScattering() {
    auto& v = volumetric_;
    if (!v.rendered) return;
    const uint32_t fi = vkCtx_->getCurrentFrame();
    // Scene pass stored this off-screen image with PRESENT layout. Synchronize
    // its writes with the load/blend operation (which also reads scene color).
    VkImageMemoryBarrier ready{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ready.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ready.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ready.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ready.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ready.srcQueueFamilyIndex = ready.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ready.image = fxaa_.sceneColor.image;
    ready.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(currentCmd_,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&ready);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = v.compositeRenderPass;
    begin.framebuffer = v.compositeFramebuffer;
    begin.renderArea.extent = fxaa_.sceneExtent;
    vkCmdBeginRenderPass(currentCmd_,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{};
    viewport.width = float(fxaa_.sceneExtent.width);
    viewport.height = float(fxaa_.sceneExtent.height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{}; scissor.extent = fxaa_.sceneExtent;
    vkCmdSetViewport(currentCmd_,0,1,&viewport);
    vkCmdSetScissor(currentCmd_,0,1,&scissor);
    vkCmdBindPipeline(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, v.debug ? v.debugPipeline : v.fogEnabled && v.fogIntensity > 0.0f ? v.fogCompositePipeline : v.compositePipeline);
    vkCmdBindDescriptorSets(currentCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, v.pipelineLayout,
        0, 1, &v.sets[fi * 3 + 2], 0, nullptr);
    vkCmdDraw(currentCmd_, 3, 1, 0, 0);
    vkCmdEndRenderPass(currentCmd_);
    // The outgoing dependency exposes the updated color to FXAA sampling.
}

void PostProcessPipeline::finishVolumetricFrame(VkCommandBuffer cmd) {
    if (!volumetric_.rendered) return;
    // Called after the output render pass, never from inside it. Preserve the
    // depth attachment layout expected by scene-history copy and continuation.
    transitionImageLayout(cmd, fxaa_.sceneDepth.image,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}
