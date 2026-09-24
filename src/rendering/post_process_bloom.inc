// Quarter-resolution highlight extraction + separable blur. No temporal history
// and no sampling from the scene attachment while compositing into it.
namespace {
struct alignas(16) BloomUniforms { glm::vec4 parameters; };
static_assert(sizeof(BloomUniforms) == 16);
}

void PostProcessPipeline::setBloomEnabled(bool enabled) {
    if (bloom_.enabled == enabled) return;
    bloom_.enabled = enabled;
    bloom_.failed = false;
}
void PostProcessPipeline::setBloomIntensity(float intensity) {
    const float clamped = clampBloomIntensity(intensity);
    if (bloom_.intensity == clamped) return;
    bloom_.intensity = clamped;
    for (bool& dirty : bloom_.compositeUniformDirty) dirty = true;
}

bool PostProcessPipeline::initBloomResources() {
    const auto device=vkCtx_->getDevice();
    const auto allocator=vkCtx_->getAllocator();
    auto& b=bloom_;
    b.extent={std::max(1u,(fxaa_.sceneExtent.width+3u)/4u),
              std::max(1u,(fxaa_.sceneExtent.height+3u)/4u)};
    VkAttachmentDescription attachment{};
    attachment.format=VK_FORMAT_R16G16B16A16_SFLOAT;
    attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1; subpass.pColorAttachments=&color;
    VkSubpassDependency dependency{};
    dependency.srcSubpass=0; dependency.dstSubpass=VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount=1; rp.pAttachments=&attachment;
    rp.subpassCount=1; rp.pSubpasses=&subpass;
    rp.dependencyCount=1; rp.pDependencies=&dependency;
    if (vkCreateRenderPass(device,&rp,nullptr,&b.renderPass)!=VK_SUCCESS) return false;
    attachment.format=vkCtx_->getSwapchainFormat();
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (vkCreateRenderPass(device,&rp,nullptr,&b.compositeRenderPass)!=VK_SUCCESS) return false;
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass=b.compositeRenderPass; fb.attachmentCount=1;
    fb.pAttachments=&fxaa_.sceneColor.imageView;
    fb.width=fxaa_.sceneExtent.width; fb.height=fxaa_.sceneExtent.height; fb.layers=1;
    if (vkCreateFramebuffer(device,&fb,nullptr,&b.compositeFramebuffer)!=VK_SUCCESS) return false;
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    b.sampler=vkCtx_->getOrCreateSampler(sampler);
    if (!b.sampler) return false;
    VkDescriptorSetLayoutBinding bindings[2]{};
    for(uint32_t i=0;i<2;++i) {
        bindings[i].binding=i; bindings[i].descriptorCount=1;
        bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].descriptorType=i ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount=2; sl.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(device,&sl,nullptr,&b.setLayout)!=VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount=1; pl.pSetLayouts=&b.setLayout;
    if(vkCreatePipelineLayout(device,&pl,nullptr,&b.pipelineLayout)!=VK_SUCCESS) return false;
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,6},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,6}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=6; pool.poolSizeCount=2; pool.pPoolSizes=sizes;
    if(vkCreateDescriptorPool(device,&pool,nullptr,&b.pool)!=VK_SUCCESS) return false;
    VkDescriptorSetLayout layouts[6]{b.setLayout,b.setLayout,b.setLayout,b.setLayout,b.setLayout,b.setLayout};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool=b.pool; alloc.descriptorSetCount=6; alloc.pSetLayouts=layouts;
    if(vkAllocateDescriptorSets(device,&alloc,b.sets)!=VK_SUCCESS) return false;
    for(uint32_t f=0;f<b.frames;++f) {
        for(uint32_t p=0;p<2;++p) {
            b.images[f][p]=createImage(device,allocator,b.extent.width,b.extent.height,
                VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
            if(!b.images[f][p].imageView) return false;
            fb.renderPass=b.renderPass; fb.pAttachments=&b.images[f][p].imageView;
            fb.width=b.extent.width; fb.height=b.extent.height;
            if(vkCreateFramebuffer(device,&fb,nullptr,&b.framebuffers[f][p])!=VK_SUCCESS) return false;
        }
        for(uint32_t p=0;p<3;++p) {
            b.uniforms[f][p]=createBuffer(allocator,sizeof(BloomUniforms),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VMA_MEMORY_USAGE_CPU_TO_GPU);
            if(!b.uniforms[f][p].info.pMappedData) return false;
            VkDescriptorImageInfo image{b.sampler,p==0 ? fxaa_.sceneColor.imageView : b.images[f][p-1].imageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            BloomUniforms initial{};
            initial.parameters=p==0 ? glm::vec4(1.0f/b.extent.width,0.0f,1.0f,0.0f) :
                p==1 ? glm::vec4(0.0f,1.0f/b.extent.height,0.0f,0.0f) : glm::vec4(0.0f,0.0f,0.0f,b.intensity);
            std::memcpy(b.uniforms[f][p].info.pMappedData,&initial,sizeof(initial));
            vmaFlushAllocation(allocator,b.uniforms[f][p].allocation,0,sizeof(initial));
            VkDescriptorBufferInfo buffer{b.uniforms[f][p].buffer,0,sizeof(BloomUniforms)};
            VkWriteDescriptorSet writes[2]{};
            for(uint32_t i=0;i<2;++i) {
                writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet=b.sets[f*3+p]; writes[i].dstBinding=i;
                writes[i].descriptorCount=1;
                writes[i].descriptorType=bindings[i].descriptorType;
            }
            writes[0].pImageInfo=&image; writes[1].pBufferInfo=&buffer;
            vkUpdateDescriptorSets(device,2,writes,0,nullptr);
        }
        b.compositeUniformDirty[f]=false;
    }
    VkShaderModule vertex,blur,composite;
    if(!vertex.loadFromFile(device,"assets/shaders/postprocess.vert.spv") ||
       !blur.loadFromFile(device,"assets/shaders/bloom_blur.frag.spv") ||
       !composite.loadFromFile(device,"assets/shaders/bloom_composite.frag.spv")) {
        vertex.destroy();blur.destroy();composite.destroy();return false;
    }
    auto build=[&](VkShaderModule& fragment,VkRenderPass pass,bool blend) {
        return PipelineBuilder().setShaders(vertex.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),fragment.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
            .setVertexInput({},{}).setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setRasterization(VK_POLYGON_MODE_FILL,VK_CULL_MODE_NONE).setNoDepthTest()
            .setColorBlendAttachment(blend ? volumetricCompositeBlend() : PipelineBuilder::blendDisabled())
            .setMultisample(VK_SAMPLE_COUNT_1_BIT).setLayout(b.pipelineLayout)
            .setRenderPass(pass).setDynamicStates(viewportAndScissorDynamic())
            .build(device,vkCtx_->getPipelineCache());
    };
    b.blurPipeline=build(blur,b.renderPass,false);
    b.compositePipeline=build(composite,b.compositeRenderPass,true);
    vertex.destroy();blur.destroy();composite.destroy();
    if(!b.blurPipeline || !b.compositePipeline) return false;
    VkDeviceSize imageBytes=0;
    for(auto& frame:b.images) for(auto& image:frame) {
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(allocator,image.allocation,&info);
        imageBytes+=info.size;
    }
    LOG_INFO("[BLOOM] ready quarterExtent=",b.extent.width,"x",b.extent.height,
        " passes=3 extractionTaps=5 blurTaps=3 intensity=",b.intensity," imageAllocationBytes=",imageBytes,
        " source=ldr-scene-before-ui status=ready");
    return true;
}

void PostProcessPipeline::destroyBloomResources() {
    if(!vkCtx_) return;
    const auto device=vkCtx_->getDevice(); const auto allocator=vkCtx_->getAllocator();
    auto& b=bloom_;
    destroy(device,b.blurPipeline);destroy(device,b.compositePipeline);
    destroy(device,b.pipelineLayout);destroy(device,b.pool);destroy(device,b.setLayout);
    for(uint32_t f=0;f<b.frames;++f) {
        for(uint32_t p=0;p<2;++p) {
            if(b.framebuffers[f][p]) vkDestroyFramebuffer(device,b.framebuffers[f][p],nullptr);
            b.framebuffers[f][p]=VK_NULL_HANDLE;
            destroyImage(device,allocator,b.images[f][p]);
        }
        for(auto& u:b.uniforms[f]) destroyBuffer(allocator,u);
    }
    if(b.compositeFramebuffer) vkDestroyFramebuffer(device,b.compositeFramebuffer,nullptr);
    b.compositeFramebuffer=VK_NULL_HANDLE;
    if(b.compositeRenderPass) vkDestroyRenderPass(device,b.compositeRenderPass,nullptr);
    if(b.renderPass) vkDestroyRenderPass(device,b.renderPass,nullptr);
    b.compositeRenderPass=b.renderPass=VK_NULL_HANDLE;
    b.sampler=VK_NULL_HANDLE;b.rendered=false;
    for(auto& set:b.sets) set=VK_NULL_HANDLE;
}

void PostProcessPipeline::renderBloom() {
    auto& b=bloom_;
    if(!b.enabled || b.intensity<=0.0f || !worldRendering_ || fsr2_.enabled ||
       !b.blurPipeline || !b.compositePipeline || currentCmd_==VK_NULL_HANDLE) return;
    // Shadow inspection modes must remain unmodified by bloom.
    if(volumetric_.debug!=0) { b.status="inspection-bypass";return; }
    const uint32_t f=vkCtx_->getCurrentFrame();
    // Extraction and blur texel steps are immutable for the lifetime of these
    // resources and were uploaded during init. The composite intensity changes
    // only when the setting changes; update the current fence-safe frame slot
    // once instead of writing/flushing three UBOs every frame.
    if (b.compositeUniformDirty[f]) {
        BloomUniforms data{};
        data.parameters=glm::vec4(0.0f,0.0f,0.0f,b.intensity);
        std::memcpy(b.uniforms[f][2].info.pMappedData,&data,sizeof(data));
        vmaFlushAllocation(vkCtx_->getAllocator(),b.uniforms[f][2].allocation,0,sizeof(data));
        b.compositeUniformDirty[f]=false;
        ++b.uniformRefreshes;
    }
    for(uint32_t p=0;p<3;++p) {
        if(p==2) {
            transitionImageLayout(currentCmd_,fxaa_.sceneColor.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        }
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass=p==2 ? b.compositeRenderPass : b.renderPass;
        rp.framebuffer=p==2 ? b.compositeFramebuffer : b.framebuffers[f][p];
        rp.renderArea.extent=p==2 ? fxaa_.sceneExtent : b.extent;
        vkCmdBeginRenderPass(currentCmd_,&rp,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{};viewport.width=float(rp.renderArea.extent.width);
        viewport.height=float(rp.renderArea.extent.height);viewport.maxDepth=1.0f;
        VkRect2D scissor{{0,0},rp.renderArea.extent};
        vkCmdSetViewport(currentCmd_,0,1,&viewport);vkCmdSetScissor(currentCmd_,0,1,&scissor);
        vkCmdBindPipeline(currentCmd_,VK_PIPELINE_BIND_POINT_GRAPHICS,p==2 ? b.compositePipeline : b.blurPipeline);
        vkCmdBindDescriptorSets(currentCmd_,VK_PIPELINE_BIND_POINT_GRAPHICS,b.pipelineLayout,
            0,1,&b.sets[f*3+p],0,nullptr);
        vkCmdDraw(currentCmd_,3,1,0,0);
        vkCmdEndRenderPass(currentCmd_);
    }
    b.rendered=true;b.status="rendered";
}
