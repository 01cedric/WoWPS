/* Host execution of production image/sampler constructors with bundled GNM.
 * Descriptor addresses are encoded only; fake GPU memory is never accessed. */
#include "vk_ps4_internal.h"
#include "vk_ps4_texture_address.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
uintptr_t __stack_chk_guard = 0x257;
GnmGpuMode sceGnmGpuMode(void) { return GNM_GPU_BASE; }
void vk_ps4_log(const char *fmt, ...) { (void)fmt; }
void *vk_ps4_alloc_zero(const VkAllocationCallbacks *a, size_t s, size_t align) {
    (void)a; (void)align; return calloc(1, s);
}
void vk_ps4_free(const VkAllocationCallbacks *a, void *p) { (void)a; free(p); }
int main(void) {
    VkPs4Device *dev = calloc(1, sizeof(*dev)); assert(dev);
    const uint32_t sizes[][2] = {
        {1536,1536}, {1024,1024}, {960,540}, {1920,1080},
        /* actual two-cascade atlas dimensions, plus PS4 scene depth. */
        {1024,512}, {2048,1024}, {3072,1536}, {1280,720}
    };
    for (unsigned i=0; i<sizeof(sizes)/sizeof(sizes[0]); ++i) {
        VkImageCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_D32_SFLOAT,
            .extent={sizes[i][0],sizes[i][1],1},.mipLevels=1,.arrayLayers=1,
            .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
        VkImage image; assert(vk_ps4_CreateImage((VkDevice)dev,&ci,NULL,&image)==VK_SUCCESS);
        VkPs4Image *img=(VkPs4Image*)image;
        VkMemoryRequirements req;vk_ps4_GetImageMemoryRequirements((VkDevice)dev,image,&req);
        VkPs4DeviceMemory mem={0};mem.size=req.size;mem.gnm_mem.mapped=(void*)UINT64_C(0x200000000);
        assert(vk_ps4_BindImageMemory((VkDevice)dev,image,(VkDeviceMemory)&mem,0)==VK_SUCCESS);
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ci.format,
            .subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
        VkImageView view;assert(vk_ps4_CreateImageView((VkDevice)dev,&vi,NULL,&view)==VK_SUCCESS);
        VkPs4ImageView *v=(VkPs4ImageView*)view;
        assert(v->gnm_view.tilingindex==img->gnm_drt.zinfo.tilemodeindex);
        assert(sceGnmTexGetPitch(&v->gnm_view)==sceGnmDrtGetPaddedWidth(&img->gnm_drt));
        assert(vk_ps4_texture_decoded_base_address(&v->gnm_view)==(uintptr_t)mem.gnm_mem.mapped);
        assert(sceGnmDrtGetZReadAddress(&img->gnm_drt)==mem.gnm_mem.mapped);
        assert(sceGnmDrtGetZWriteAddress(&img->gnm_drt)==mem.gnm_mem.mapped);
        assert(v->gnm_view.basearray==0 && v->gnm_view.lastarray==0);
        assert(v->gnm_view.baselevel==0 && v->gnm_view.lastlevel==0);
        assert(v->gnm_view.dataformat==GNM_IMG_DATA_FORMAT_32);
        assert(v->gnm_view.numformat==GNM_IMG_NUM_FORMAT_FLOAT);
        assert(v->gnm_view.dstselx==GNM_CHAN_X);
        assert(!img->gnm_drt.zinfo.tilesurfaceenable && !img->gnm_drt.htiledatabase256b);
        printf("PASS production sampled D32 %ux%u: size=%llu pitch=%u tile=%u base=0x%llx no HTILE\n",
            sizes[i][0],sizes[i][1],(unsigned long long)req.size,sceGnmTexGetPitch(&v->gnm_view),
            v->gnm_view.tilingindex,(unsigned long long)vk_ps4_texture_decoded_base_address(&v->gnm_view));
        vk_ps4_DestroyImageView((VkDevice)dev,view,NULL);vk_ps4_DestroyImage((VkDevice)dev,image,NULL);
    }
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .compareEnable=VK_TRUE,.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE};
    VkSampler sampler;assert(vk_ps4_CreateSampler((VkDevice)dev,&si,NULL,&sampler)==VK_SUCCESS);
    assert(((GnmSampler*)sampler)->depthcomparefunc==3);
    assert(((GnmSampler*)sampler)->bordercolortype==GNM_BORDER_COLOR_OPAQUE_WHITE);
    vk_ps4_DestroySampler((VkDevice)dev,sampler,NULL);
    si.compareEnable=VK_FALSE;assert(vk_ps4_CreateSampler((VkDevice)dev,&si,NULL,&sampler)==VK_SUCCESS);
    assert(((GnmSampler*)sampler)->depthcomparefunc==7);
    vk_ps4_DestroySampler((VkDevice)dev,sampler,NULL);
    puts("PASS production compare LEQUAL/white border and raw sampler descriptors");free(dev);
}
