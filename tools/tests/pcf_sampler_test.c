/* Inspect the actual ICD's emitted GNM sampler, not a duplicate mapping. */
#include "vk_ps4_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
void vk_ps4_log(const char *fmt, ...) { (void)fmt; }
void *vk_ps4_alloc_zero(const VkAllocationCallbacks *alloc, size_t size, size_t alignment) {
    (void)alloc; (void)alignment; return calloc(1,size);
}
void vk_ps4_free(const VkAllocationCallbacks *alloc, void *pointer) {
    (void)alloc; free(pointer);
}
int main(void) {
    VkPs4Device device = {0};
    VkSamplerCreateInfo ci = {0};
    ci.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.minFilter=ci.magFilter=VK_FILTER_LINEAR;
    ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    ci.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    ci.compareEnable=VK_TRUE;
    ci.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkSampler sampler=VK_NULL_HANDLE;
    assert(vk_ps4_CreateSampler((VkDevice)&device,&ci,NULL,&sampler)==VK_SUCCESS);
    const GnmSampler *gnm=(const GnmSampler*)sampler;
    assert(gnm->xymagfilter==GNM_FILTER_BILINEAR && gnm->xyminfilter==GNM_FILTER_BILINEAR);
    assert(gnm->depthcomparefunc==3 && gnm->clampx==GNM_TEX_CLAMP_CLAMP_BORDER);
    assert(gnm->clampy==GNM_TEX_CLAMP_CLAMP_BORDER && gnm->bordercolortype==2);
    vk_ps4_DestroySampler((VkDevice)&device,sampler,NULL);
    puts("PASS production ICD sampler: bilinear depth comparison LESS_OR_EQUAL with white border");
}
