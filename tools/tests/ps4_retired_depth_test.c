/* Production bounded reader over real GNM-tiled CPU fixtures; no GPU claims. */
#include "vk_ps4_internal.h"
#include "vk_ps4.h"
#include "gpuaddr.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
uintptr_t __stack_chk_guard = 0x264;
GnmGpuMode sceGnmGpuMode(void) { return GNM_GPU_BASE; }
void vk_ps4_log(const char *fmt, ...) { (void)fmt; }
void *vk_ps4_alloc_zero(const VkAllocationCallbacks *a, size_t s, size_t align) {
    (void)a; (void)align; return calloc(1,s);
}
void vk_ps4_free(const VkAllocationCallbacks *a, void *p) { (void)a; free(p); }
int main(void) {
    VkPs4Device *dev=calloc(1,sizeof(*dev)); assert(dev);
    const uint32_t sides[]={512,1024,1536};
    for(unsigned c=0;c<3;++c) {
        const uint32_t n=sides[c],w=n*2,h=n;
        VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_D32_SFLOAT,
            .extent={w,h,1},.mipLevels=1,.arrayLayers=1,
            .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
        VkImage image; assert(vk_ps4_CreateImage((VkDevice)dev,&ci,NULL,&image)==VK_SUCCESS);
        VkPs4Image *img=(VkPs4Image*)image;
        VkMemoryRequirements req; vk_ps4_GetImageMemoryRequirements((VkDevice)dev,image,&req);
        VkPs4DeviceMemory mem={0};mem.size=req.size+65536;
        mem.memory_type_index=VK_PS4_MEMORY_TYPE_GARLIC;
        void *storage=aligned_alloc(65536,(mem.size+65535)&~UINT64_C(65535));assert(storage);
        /* Host ASLR/ASAN pointers need not fit PS4 depth-address fields. Encode
         * a console-range address, then attach host backing for the CPU read. */
        mem.gnm_mem.mapped=(void*)UINT64_C(0x200000000);
        assert(vk_ps4_BindImageMemory((VkDevice)dev,image,(VkDeviceMemory)&mem,65536)==VK_SUCCESS);
        mem.gnm_mem.mapped=storage;
        float *linear=malloc((size_t)w*h*4);assert(linear);
        for(size_t k=0;k<(size_t)w*h;++k)linear[k]=1.0f;
        /* All samples in near region remain clear; a small far region contains
         * known valid, out-of-range and non-finite values. */
        linear[n]=0.25f;linear[n+1]=NAN;linear[n+2]=-0.5f;linear[n+3]=1.5f;
        GpaTextureInfo dst=sceGnmTexBuildInfo(&img->gnm_texture),src=dst;
        src.tm=GNM_TM_DISPLAY_LINEAR_GENERAL;src.pitch=w;
        GpaTilingParams st,dt;
        assert(sceGpaTpInit(&st,&src,0,0)==GPA_ERR_OK);
        assert(sceGpaTpInit(&dt,&dst,0,0)==GPA_ERR_OK);
        assert(sceGpaTileSurface((char*)mem.gnm_mem.mapped+65536,req.size,
            linear,(size_t)w*h*4,&st,&dt)==GPA_ERR_OK);
        VkPs4DepthInspection out;
        VkRect2D near={{0,0},{n,n}},far={{(int32_t)n,0},{16,16}};
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&near,&out)==VK_SUCCESS);
        assert(out.rejectionReason==0 && out.memoryTypeIndex==VK_PS4_MEMORY_TYPE_GARLIC);
        assert(out.sampleCount==1024 && out.finiteCount==1024 && !out.nonClearCount && !out.invalidCount);
        assert(out.minDepth==1 && out.maxDepth==1);
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&far,&out)==VK_SUCCESS);
        assert(out.sampleCount==256 && out.finiteCount==255 && out.nonClearCount==1 && out.invalidCount==3);
        assert(out.minDepth==-0.5f && out.maxDepth==1.5f);
        VkRect2D bad={{-1,0},{1,1}};
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&bad,&out)!=VK_SUCCESS && !out.sampleCount);
        bad=(VkRect2D){{(int32_t)w-1,0},{2,1}};
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&bad,&out)!=VK_SUCCESS);
        bad=(VkRect2D){{0,0},{0,1}};
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&bad,&out)!=VK_SUCCESS);
        mem.size=65536+4;
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&near,&out)!=VK_SUCCESS);
        mem.size=req.size+65536;mem.memory_type_index=VK_PS4_MEMORY_TYPE_ONION;
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&near,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
        assert(out.rejectionReason==VK_PS4_DEPTH_REJECT_MEMORY_TYPE && out.sampleCount==0 && out.memoryTypeIndex==VK_PS4_MEMORY_TYPE_ONION);
        mem.memory_type_index=VK_PS4_MEMORY_TYPE_GARLIC;img->gnm_drt.zinfo.tilesurfaceenable=1;
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&near,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
        assert(out.rejectionReason==VK_PS4_DEPTH_REJECT_HTILE && out.sampleCount==0);
        img->gnm_drt.zinfo.tilesurfaceenable=0;
        assert(vk_ps4_InspectRetiredDepthImage(VK_NULL_HANDLE,image,&near,&out)!=VK_SUCCESS);
        assert(vk_ps4_InspectRetiredDepthImage((VkDevice)dev,image,&near,NULL)!=VK_SUCCESS);
        printf("PASS %ux%u tiled atlas: near clear, far known/nonfinite/out-of-range, nonzero allocation offset, bounded invalid requests\n",w,h);
        free(linear);free(mem.gnm_mem.mapped);vk_ps4_DestroyImage((VkDevice)dev,image,NULL);
    }
    free(dev);puts("PASS retired D32 inspection (CPU fixture only; caller fence precondition not GPU-tested)");
}
