/* Host execution of production image/sampler constructors with bundled GNM.
 * Descriptor addresses are encoded only; fake GPU memory is never accessed. */
#include "vk_ps4_internal.h"
#include "vk_ps4_texture_address.h"
#include <assert.h>
#include "gpuaddr.h"
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
        {320,180}, {640,360}, {641,361}, {1024,1024}, {960,540}, {1920,1080},
        /* actual two-cascade atlas dimensions, plus PS4 scene depth. */
        {1024,512}, {2048,1024}, {3072,1536}, {1280,720}
    };
    const VkFormat formats[]={VK_FORMAT_R16_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32_SFLOAT};
    for(unsigned f=0;f<sizeof(formats)/sizeof(formats[0]);++f)
    for (unsigned i=0; i<sizeof(sizes)/sizeof(sizes[0]); ++i) {
        VkImageCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType=VK_IMAGE_TYPE_2D,.format=formats[f],
            .extent={sizes[i][0],sizes[i][1],1},.mipLevels=1,.arrayLayers=1,
            .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
        VkImage image; assert(vk_ps4_CreateImage((VkDevice)dev,&ci,NULL,&image)==VK_SUCCESS);
        VkPs4Image *img=(VkPs4Image*)image;
        VkMemoryRequirements req;vk_ps4_GetImageMemoryRequirements((VkDevice)dev,image,&req);
        VkPs4DeviceMemory mem={0};mem.size=req.size;mem.gnm_mem.mapped=(void*)UINT64_C(0x200000000);
        assert(vk_ps4_BindImageMemory((VkDevice)dev,image,(VkDeviceMemory)&mem,0)==VK_SUCCESS);
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ci.format,
            .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        VkImageView view;assert(vk_ps4_CreateImageView((VkDevice)dev,&vi,NULL,&view)==VK_SUCCESS);
        VkPs4ImageView *v=(VkPs4ImageView*)view;
        assert(img->is_render_target && !img->is_depth_target);
        assert(v->gnm_view.tilingindex==img->gnm_rt.attrib.tilemode_index);
        assert(sceGnmTexGetPitch(&v->gnm_view)==sceGnmRtGetPitch(&img->gnm_rt));
        assert(vk_ps4_texture_decoded_base_address(&v->gnm_view)==(uintptr_t)mem.gnm_mem.mapped);
        assert(sceGnmRtGetBaseAddr(&img->gnm_rt)==mem.gnm_mem.mapped);
        const GnmDataFormat expected=vk_ps4_vk_format_to_gnm(ci.format);
        assert(v->gnm_view.dataformat==expected.surfacefmt && v->gnm_view.numformat==expected.chantype);
        assert(v->gnm_view.dstselx==expected.chanx && v->gnm_view.dstsely==expected.chany && v->gnm_view.dstselz==expected.chanz && v->gnm_view.dstselw==expected.chanw);
        assert(img->gnm_rt.info.format==expected.surfacefmt);
        /* Compare independent RT/texture descriptors at every actual pixel.
         * This validates native address math, not physical GPU shader execution. */
        GpaTextureInfo ri = sceGnmRtBuildInfo(&img->gnm_rt);
        GpaTextureInfo ti = sceGnmTexBuildInfo(&v->gnm_view);
        GpaTilingParams rp, tp;
        assert(sceGpaTpInit(&rp, &ri, 0, 0) == GPA_ERR_OK);
        assert(sceGpaTpInit(&tp, &ti, 0, 0) == GPA_ERR_OK);
        GpaSurfaceContext rc, tc;
        assert(sceGpaInitSurfaceContext(&rc, req.size, &rp) == GPA_ERR_OK);
        assert(sceGpaInitSurfaceContext(&tc, req.size, &tp) == GPA_ERR_OK);
        for (uint32_t y = 0; y < ci.extent.height; ++y)
        for (uint32_t x = 0; x < ci.extent.width; ++x) {
            uint64_t ro, rb, to, tb;
            assert(sceGpaComputeSurfaceCoord(&ro, &rb, &rc, x, y, 0, 0) == GPA_ERR_OK);
            assert(sceGpaComputeSurfaceCoord(&to, &tb, &tc, x, y, 0, 0) == GPA_ERR_OK);
            assert(ro == to && rb == tb && ro < req.size);
        }
        printf("PASS RT/sample per-pixel address match; production sampled color target VkFormat=%u %ux%u: size=%llu pitch=%u tile=%u channels=%u,%u,%u,%u\n",
            ci.format,sizes[i][0],sizes[i][1],(unsigned long long)req.size,sceGnmTexGetPitch(&v->gnm_view),
            v->gnm_view.tilingindex,v->gnm_view.dstselx,v->gnm_view.dstsely,v->gnm_view.dstselz,v->gnm_view.dstselw);
        vk_ps4_DestroyImageView((VkDevice)dev,view,NULL);
        vi.components=(VkComponentMapping){VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_ZERO};
        assert(vk_ps4_CreateImageView((VkDevice)dev,&vi,NULL,&view)==VK_SUCCESS);v=(VkPs4ImageView*)view;
        assert(v->gnm_view.dstselx==expected.chanz && v->gnm_view.dstsely==GNM_CHAN_CONSTANT1 && v->gnm_view.dstselz==expected.chanx && v->gnm_view.dstselw==GNM_CHAN_CONSTANT0);
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
