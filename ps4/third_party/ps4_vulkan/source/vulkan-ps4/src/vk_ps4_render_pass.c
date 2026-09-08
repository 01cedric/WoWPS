/*
 * vk_ps4_render_pass.c — VkRenderPass / VkFramebuffer implementation.
 *
 * VkRenderPass stores attachment descriptions and subpass info.
 * VkFramebuffer links attachment image views.
 * vkCmdBeginRenderPass emits render target set + clear operations.
 * vkCmdEndRenderPass emits EOP event.
 */

#include "vk_ps4_internal.h"

#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    if (!device || !pCreateInfo || !pRenderPass) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4RenderPass *rp = vk_ps4_alloc_zero(alloc, sizeof(*rp), 16);
    if (!rp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    rp->type = VK_PS4_OBJ_RENDER_PASS;
    rp->device = dev;
    rp->create_info = *pCreateInfo;  /* shallow copy, pointers fixed below */
    rp->attachment_count = pCreateInfo->attachmentCount;
    rp->subpass_count = pCreateInfo->subpassCount;
    rp->subpass_dependency_count = pCreateInfo->dependencyCount;

    /* Deep copy attachments and wire into create_info */
    if (rp->attachment_count > 0) {
        rp->attachments = vk_ps4_alloc_zero(alloc,
            rp->attachment_count * sizeof(VkAttachmentDescription), 16);
        if (!rp->attachments) {
            vk_ps4_free(alloc, rp);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(rp->attachments, pCreateInfo->pAttachments,
               rp->attachment_count * sizeof(VkAttachmentDescription));
        rp->create_info.pAttachments = rp->attachments;
    }

    /* Deep copy subpasses — including all internal pointer arrays.
     * Without this, pColorAttachments / pDepthStencilAttachment / etc.
     * would dangle after the caller frees pCreateInfo. */
    if (rp->subpass_count > 0) {
        rp->subpasses = vk_ps4_alloc_zero(alloc,
            rp->subpass_count * sizeof(VkSubpassDescription), 16);
        if (!rp->subpasses) goto fail_attachments;
        memcpy(rp->subpasses, pCreateInfo->pSubpasses,
               rp->subpass_count * sizeof(VkSubpassDescription));
        rp->create_info.pSubpasses = rp->subpasses;

        /* Allocate per-subpass pointer arrays for deep-copied internal data */
        rp->subpass_input_attachments = vk_ps4_alloc_zero(alloc,
            rp->subpass_count * sizeof(VkAttachmentReference *), 16);
        rp->subpass_color_attachments = vk_ps4_alloc_zero(alloc,
            rp->subpass_count * sizeof(VkAttachmentReference *), 16);
        rp->subpass_resolve_attachments = vk_ps4_alloc_zero(alloc,
            rp->subpass_count * sizeof(VkAttachmentReference *), 16);
        rp->subpass_depth_stencil = vk_ps4_alloc_zero(alloc,
            rp->subpass_count * sizeof(VkAttachmentReference *), 16);
        rp->subpass_preserve_attachments = vk_ps4_alloc_zero(alloc,
            rp->subpass_count * sizeof(uint32_t *), 16);
        if (!rp->subpass_input_attachments || !rp->subpass_color_attachments ||
            !rp->subpass_resolve_attachments || !rp->subpass_depth_stencil ||
            !rp->subpass_preserve_attachments) {
            goto fail_subpass_arrays;
        }

        for (uint32_t i = 0; i < rp->subpass_count; i++) {
            const VkSubpassDescription *src = &pCreateInfo->pSubpasses[i];
            VkSubpassDescription *dst = &rp->subpasses[i];

            if (src->inputAttachmentCount > 0 && src->pInputAttachments) {
                rp->subpass_input_attachments[i] = vk_ps4_alloc_zero(alloc,
                    src->inputAttachmentCount * sizeof(VkAttachmentReference), 16);
                if (!rp->subpass_input_attachments[i]) goto fail_subpass_arrays;
                memcpy(rp->subpass_input_attachments[i], src->pInputAttachments,
                       src->inputAttachmentCount * sizeof(VkAttachmentReference));
                dst->pInputAttachments = rp->subpass_input_attachments[i];
            } else {
                dst->pInputAttachments = NULL;
                dst->inputAttachmentCount = 0;
            }

            if (src->colorAttachmentCount > 0 && src->pColorAttachments) {
                rp->subpass_color_attachments[i] = vk_ps4_alloc_zero(alloc,
                    src->colorAttachmentCount * sizeof(VkAttachmentReference), 16);
                if (!rp->subpass_color_attachments[i]) goto fail_subpass_arrays;
                memcpy(rp->subpass_color_attachments[i], src->pColorAttachments,
                       src->colorAttachmentCount * sizeof(VkAttachmentReference));
                dst->pColorAttachments = rp->subpass_color_attachments[i];
            } else {
                dst->pColorAttachments = NULL;
                dst->colorAttachmentCount = 0;
            }

            if (src->colorAttachmentCount > 0 && src->pResolveAttachments) {
                rp->subpass_resolve_attachments[i] = vk_ps4_alloc_zero(alloc,
                    src->colorAttachmentCount * sizeof(VkAttachmentReference), 16);
                if (!rp->subpass_resolve_attachments[i]) goto fail_subpass_arrays;
                memcpy(rp->subpass_resolve_attachments[i], src->pResolveAttachments,
                       src->colorAttachmentCount * sizeof(VkAttachmentReference));
                dst->pResolveAttachments = rp->subpass_resolve_attachments[i];
            } else {
                dst->pResolveAttachments = NULL;
            }

            if (src->pDepthStencilAttachment) {
                rp->subpass_depth_stencil[i] = vk_ps4_alloc_zero(alloc,
                    sizeof(VkAttachmentReference), 16);
                if (!rp->subpass_depth_stencil[i]) goto fail_subpass_arrays;
                memcpy(rp->subpass_depth_stencil[i], src->pDepthStencilAttachment,
                       sizeof(VkAttachmentReference));
                dst->pDepthStencilAttachment = rp->subpass_depth_stencil[i];
            } else {
                dst->pDepthStencilAttachment = NULL;
            }

            if (src->preserveAttachmentCount > 0 && src->pPreserveAttachments) {
                rp->subpass_preserve_attachments[i] = vk_ps4_alloc_zero(alloc,
                    src->preserveAttachmentCount * sizeof(uint32_t), 16);
                if (!rp->subpass_preserve_attachments[i]) goto fail_subpass_arrays;
                memcpy(rp->subpass_preserve_attachments[i], src->pPreserveAttachments,
                       src->preserveAttachmentCount * sizeof(uint32_t));
                dst->pPreserveAttachments = rp->subpass_preserve_attachments[i];
            } else {
                dst->pPreserveAttachments = NULL;
                dst->preserveAttachmentCount = 0;
            }
        }
    }

    /* Deep copy dependencies */
    if (rp->subpass_dependency_count > 0 && pCreateInfo->pDependencies) {
        rp->dependencies = vk_ps4_alloc_zero(alloc,
            rp->subpass_dependency_count * sizeof(VkSubpassDependency), 16);
        if (!rp->dependencies) goto fail_subpass_arrays;
        memcpy(rp->dependencies, pCreateInfo->pDependencies,
               rp->subpass_dependency_count * sizeof(VkSubpassDependency));
        rp->create_info.pDependencies = rp->dependencies;
    } else {
        rp->create_info.pDependencies = NULL;
        rp->create_info.dependencyCount = 0;
    }

    *pRenderPass = (VkRenderPass)rp;
    return VK_SUCCESS;

fail_subpass_arrays:
    for (uint32_t i = 0; i < rp->subpass_count; i++) {
        vk_ps4_free(alloc, rp->subpass_input_attachments ? rp->subpass_input_attachments[i] : NULL);
        vk_ps4_free(alloc, rp->subpass_color_attachments ? rp->subpass_color_attachments[i] : NULL);
        vk_ps4_free(alloc, rp->subpass_resolve_attachments ? rp->subpass_resolve_attachments[i] : NULL);
        vk_ps4_free(alloc, rp->subpass_depth_stencil ? rp->subpass_depth_stencil[i] : NULL);
        vk_ps4_free(alloc, rp->subpass_preserve_attachments ? rp->subpass_preserve_attachments[i] : NULL);
    }
    vk_ps4_free(alloc, rp->subpass_input_attachments);
    vk_ps4_free(alloc, rp->subpass_color_attachments);
    vk_ps4_free(alloc, rp->subpass_resolve_attachments);
    vk_ps4_free(alloc, rp->subpass_depth_stencil);
    vk_ps4_free(alloc, rp->subpass_preserve_attachments);
    vk_ps4_free(alloc, rp->subpasses);
fail_attachments:
    vk_ps4_free(alloc, rp->attachments);
    vk_ps4_free(alloc, rp);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator) {
    if (!device || !renderPass) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4RenderPass *rp = (VkPs4RenderPass *)renderPass;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    /* Free deep-copied subpass internal arrays */
    if (rp->subpass_input_attachments) {
        for (uint32_t i = 0; i < rp->subpass_count; i++)
            vk_ps4_free(alloc, rp->subpass_input_attachments[i]);
        vk_ps4_free(alloc, rp->subpass_input_attachments);
    }
    if (rp->subpass_color_attachments) {
        for (uint32_t i = 0; i < rp->subpass_count; i++)
            vk_ps4_free(alloc, rp->subpass_color_attachments[i]);
        vk_ps4_free(alloc, rp->subpass_color_attachments);
    }
    if (rp->subpass_resolve_attachments) {
        for (uint32_t i = 0; i < rp->subpass_count; i++)
            vk_ps4_free(alloc, rp->subpass_resolve_attachments[i]);
        vk_ps4_free(alloc, rp->subpass_resolve_attachments);
    }
    if (rp->subpass_depth_stencil) {
        for (uint32_t i = 0; i < rp->subpass_count; i++)
            vk_ps4_free(alloc, rp->subpass_depth_stencil[i]);
        vk_ps4_free(alloc, rp->subpass_depth_stencil);
    }
    if (rp->subpass_preserve_attachments) {
        for (uint32_t i = 0; i < rp->subpass_count; i++)
            vk_ps4_free(alloc, rp->subpass_preserve_attachments[i]);
        vk_ps4_free(alloc, rp->subpass_preserve_attachments);
    }
    vk_ps4_free(alloc, rp->dependencies);
    vk_ps4_free(alloc, rp->attachments);
    vk_ps4_free(alloc, rp->subpasses);
    vk_ps4_free(alloc, rp);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer) {
    if (!device || !pCreateInfo || !pFramebuffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Framebuffer *fb = vk_ps4_alloc_zero(alloc, sizeof(*fb), 16);
    if (!fb) return VK_ERROR_OUT_OF_HOST_MEMORY;
    fb->type = VK_PS4_OBJ_FRAMEBUFFER;
    fb->device = dev;
    fb->render_pass = (VkPs4RenderPass *)pCreateInfo->renderPass;
    fb->create_info = *pCreateInfo;  /* shallow copy, pointers fixed below */
    fb->attachment_count = pCreateInfo->attachmentCount;
    fb->width = pCreateInfo->width;
    fb->height = pCreateInfo->height;
    fb->layers = pCreateInfo->layers;

    /* VK_KHR_imageless_framebuffer: when IMAGELESS_BIT is set, pAttachments
     * is not provided at framebuffer creation time.  The actual image views
     * are supplied at CmdBeginRenderPass time via VkRenderPassAttachmentBeginInfo. */
    fb->imageless = (pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT_KHR) != 0;

    if (fb->imageless) {
        /* No attachments stored — views come at beginRenderPass time */
        fb->attachments = NULL;
        fb->create_info.pAttachments = NULL;
    } else if (fb->attachment_count > 0) {
        /* Store attachment image view pointers and wire into create_info */
        fb->attachments = vk_ps4_alloc_zero(alloc,
            fb->attachment_count * sizeof(VkPs4ImageView *), 16);
        if (!fb->attachments) {
            vk_ps4_free(alloc, fb);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        for (uint32_t i = 0; i < fb->attachment_count; i++) {
            fb->attachments[i] = (VkPs4ImageView *)pCreateInfo->pAttachments[i];
        }
        /* Note: create_info.pAttachments still points to caller's VkImageView array.
         * We don't use it after creation — we use fb->attachments instead.
         * Null it out to avoid accidental UAF. */
        fb->create_info.pAttachments = NULL;
    }

    *pFramebuffer = (VkFramebuffer)fb;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator) {
    if (!device || !framebuffer) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Framebuffer *fb = (VkPs4Framebuffer *)framebuffer;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, fb->attachments);
    vk_ps4_free(alloc, fb);
}

/* === VK_KHR_create_renderpass2 === */
/* Convert VkRenderPassCreateInfo2 → VkRenderPassCreateInfo and delegate
 * to the existing CreateRenderPass.  The v2 structures are supersets of
 * v1 — we strip the extra fields (pNext, flags, aspectMask) and convert
 * attachment refs to v1 format. */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    if (!device || !pCreateInfo || !pRenderPass) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Convert attachments */
    VkAttachmentDescription *atts = NULL;
    if (pCreateInfo->attachmentCount > 0) {
        atts = vk_ps4_alloc_zero(alloc,
            pCreateInfo->attachmentCount * sizeof(VkAttachmentDescription), 16);
        if (!atts) return VK_ERROR_OUT_OF_HOST_MEMORY;
        for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
            const VkAttachmentDescription2 *s = &pCreateInfo->pAttachments[i];
            atts[i].flags = s->flags;
            atts[i].format = s->format;
            atts[i].samples = s->samples;
            atts[i].loadOp = s->loadOp;
            atts[i].storeOp = s->storeOp;
            atts[i].stencilLoadOp = s->stencilLoadOp;
            atts[i].stencilStoreOp = s->stencilStoreOp;
            atts[i].initialLayout = s->initialLayout;
            atts[i].finalLayout = s->finalLayout;
        }
    }

    /* Convert subpasses */
    VkSubpassDescription *subs = NULL;
    VkAttachmentReference **sub_refs = NULL;  /* arrays for each subpass */
    uint32_t **sub_preserve = NULL;
    /* We need to allocate arrays for input/color/resolve/ds refs per subpass.
     * Use a simple approach: allocate all arrays up front. */
    if (pCreateInfo->subpassCount > 0) {
        subs = vk_ps4_alloc_zero(alloc,
            pCreateInfo->subpassCount * sizeof(VkSubpassDescription), 16);
        if (!subs) goto rp2_fail_atts;
        /* Allocate per-subpass ref arrays */
        sub_refs = vk_ps4_alloc_zero(alloc,
            pCreateInfo->subpassCount * 4 * sizeof(VkAttachmentReference *), 16);
        if (!sub_refs) goto rp2_fail_subs;
        sub_preserve = vk_ps4_alloc_zero(alloc,
            pCreateInfo->subpassCount * sizeof(uint32_t *), 16);
        if (!sub_preserve) goto rp2_fail_subrefs;
    }

    for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
        const VkSubpassDescription2 *s = &pCreateInfo->pSubpasses[i];
        VkSubpassDescription *d = &subs[i];
        d->flags = s->flags;
        d->pipelineBindPoint = s->pipelineBindPoint;
        d->inputAttachmentCount = s->inputAttachmentCount;
        d->colorAttachmentCount = s->colorAttachmentCount;
        d->preserveAttachmentCount = s->preserveAttachmentCount;

        /* Input attachments */
        if (s->inputAttachmentCount > 0 && s->pInputAttachments) {
            VkAttachmentReference *refs = vk_ps4_alloc_zero(alloc,
                s->inputAttachmentCount * sizeof(VkAttachmentReference), 16);
            if (!refs) goto rp2_fail_all;
            for (uint32_t j = 0; j < s->inputAttachmentCount; j++) {
                refs[j].attachment = s->pInputAttachments[j].attachment;
                refs[j].layout = s->pInputAttachments[j].layout;
            }
            d->pInputAttachments = refs;
            sub_refs[i * 4 + 0] = refs;
        }
        /* Color attachments */
        if (s->colorAttachmentCount > 0 && s->pColorAttachments) {
            VkAttachmentReference *refs = vk_ps4_alloc_zero(alloc,
                s->colorAttachmentCount * sizeof(VkAttachmentReference), 16);
            if (!refs) goto rp2_fail_all;
            for (uint32_t j = 0; j < s->colorAttachmentCount; j++) {
                refs[j].attachment = s->pColorAttachments[j].attachment;
                refs[j].layout = s->pColorAttachments[j].layout;
            }
            d->pColorAttachments = refs;
            sub_refs[i * 4 + 1] = refs;
        }
        /* Resolve attachments */
        if (s->colorAttachmentCount > 0 && s->pResolveAttachments) {
            VkAttachmentReference *refs = vk_ps4_alloc_zero(alloc,
                s->colorAttachmentCount * sizeof(VkAttachmentReference), 16);
            if (!refs) goto rp2_fail_all;
            for (uint32_t j = 0; j < s->colorAttachmentCount; j++) {
                refs[j].attachment = s->pResolveAttachments[j].attachment;
                refs[j].layout = s->pResolveAttachments[j].layout;
            }
            d->pResolveAttachments = refs;
            sub_refs[i * 4 + 2] = refs;
        }
        /* Depth/stencil attachment */
        if (s->pDepthStencilAttachment) {
            VkAttachmentReference *ref = vk_ps4_alloc_zero(alloc,
                sizeof(VkAttachmentReference), 16);
            if (!ref) goto rp2_fail_all;
            ref->attachment = s->pDepthStencilAttachment->attachment;
            ref->layout = s->pDepthStencilAttachment->layout;
            d->pDepthStencilAttachment = ref;
            sub_refs[i * 4 + 3] = ref;
        }
        /* Preserve attachments */
        if (s->preserveAttachmentCount > 0 && s->pPreserveAttachments) {
            uint32_t *pres = vk_ps4_alloc_zero(alloc,
                s->preserveAttachmentCount * sizeof(uint32_t), 16);
            if (!pres) goto rp2_fail_all;
            memcpy(pres, s->pPreserveAttachments,
                   s->preserveAttachmentCount * sizeof(uint32_t));
            d->pPreserveAttachments = pres;
            sub_preserve[i] = pres;
        }
    }

    /* Convert dependencies */
    VkSubpassDependency *deps = NULL;
    if (pCreateInfo->dependencyCount > 0 && pCreateInfo->pDependencies) {
        deps = vk_ps4_alloc_zero(alloc,
            pCreateInfo->dependencyCount * sizeof(VkSubpassDependency), 16);
        if (!deps) goto rp2_fail_all;
        for (uint32_t i = 0; i < pCreateInfo->dependencyCount; i++) {
            const VkSubpassDependency2 *s = &pCreateInfo->pDependencies[i];
            deps[i].srcSubpass = s->srcSubpass;
            deps[i].dstSubpass = s->dstSubpass;
            deps[i].srcStageMask = s->srcStageMask;
            deps[i].dstStageMask = s->dstStageMask;
            deps[i].srcAccessMask = s->srcAccessMask;
            deps[i].dstAccessMask = s->dstAccessMask;
            deps[i].dependencyFlags = s->dependencyFlags;
        }
    }

    /* Build v1 create info and delegate */
    VkRenderPassCreateInfo v1ci = {0};
    v1ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    v1ci.attachmentCount = pCreateInfo->attachmentCount;
    v1ci.pAttachments = atts;
    v1ci.subpassCount = pCreateInfo->subpassCount;
    v1ci.pSubpasses = subs;
    v1ci.dependencyCount = pCreateInfo->dependencyCount;
    v1ci.pDependencies = deps;

    VkResult result = vk_ps4_CreateRenderPass(device, &v1ci, pAllocator, pRenderPass);

    /* Free conversion temporaries (CreateRenderPass deep-copies everything) */
    vk_ps4_free(alloc, deps);
    if (sub_refs) {
        for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
            for (uint32_t j = 0; j < 4; j++)
                vk_ps4_free(alloc, sub_refs[i * 4 + j]);
        }
        vk_ps4_free(alloc, sub_refs);
    }
    if (sub_preserve) {
        for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++)
            vk_ps4_free(alloc, sub_preserve[i]);
        vk_ps4_free(alloc, sub_preserve);
    }
    vk_ps4_free(alloc, subs);
    vk_ps4_free(alloc, atts);
    return result;

rp2_fail_all:
    if (sub_refs) {
        for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++)
            for (uint32_t j = 0; j < 4; j++)
                vk_ps4_free(alloc, sub_refs[i * 4 + j]);
        vk_ps4_free(alloc, sub_refs);
    }
rp2_fail_subrefs:
    vk_ps4_free(alloc, sub_preserve);
rp2_fail_subs:
    vk_ps4_free(alloc, subs);
rp2_fail_atts:
    vk_ps4_free(alloc, atts);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}
