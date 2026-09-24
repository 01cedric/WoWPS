#!/usr/bin/env python3
"""Execute the actual interrupted adapter and batch submit path with a Vulkan seam."""
from pathlib import Path
import os,shlex,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'src/rendering/vk_context.cpp').read_text()
methods=s[s.index('void VkContext::beginUploadBatch()'):s.index('/// Opens the batch')]+s[s.index('void VkContext::finishInterruptedUploadBatch()'):s.index('void VkContext::pollUploadBatches()')]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
static long failAfter=-1;
void* operator new(size_t size){if(failAfter==0)throw std::bad_alloc();if(failAfter>0)--failAfter;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
using VkDeviceSize=uint64_t;using VkResult=int;using VkCommandPool=uint64_t;using VkFence=uint64_t;using VkQueue=uint64_t;
constexpr uint64_t VK_NULL_HANDLE=0;constexpr int VK_SUCCESS=0,VK_STRUCTURE_TYPE_FENCE_CREATE_INFO=1,VK_STRUCTURE_TYPE_SUBMIT_INFO=2;
struct VkFenceCreateInfo{int sType=0;};
struct VkSubmitInfo{int sType=0;unsigned commandBufferCount=0;const uint64_t*pCommandBuffers=nullptr;};
struct Buffer{uint64_t buffer;struct{uint64_t size=0;}info;};struct Raw{uint64_t buffer,memory,bytes=0;};
static unsigned releasedCommands=0,releasedBuffers=0,releasedRaw=0,submitted=0;
static int failEnd=0,failFence=0,failSubmit=0;
VkResult vkEndCommandBuffer(uint64_t){return failEnd;}
void vkFreeCommandBuffers(uint64_t,VkCommandPool,unsigned,const uint64_t*){++releasedCommands;}
void destroyBuffer(uint64_t,Buffer& b){if(b.buffer)++releasedBuffers;b.buffer=0;}
VkResult vkCreateFence(uint64_t,const VkFenceCreateInfo*,void*,VkFence*f){if(!failFence)*f=99;return failFence;}
VkResult vkQueueSubmit(VkQueue,unsigned,const VkSubmitInfo*info,VkFence fence){assert(fence==99&&info->commandBufferCount==1&&*info->pCommandBuffers==42);++submitted;return failSubmit;}
struct VkContext {
    struct InFlightBatch{VkFence fence=0;uint64_t cmd=0;bool separateQueue=false;std::vector<Buffer>stagingBuffers;std::vector<Raw>rawStaging;VkDeviceSize stagingBytes=0;};
    int uploadBatchDepth_=0;bool inUploadBatch_=false,hasDedicatedTransfer_=false,deviceLost_=false;
    uint64_t batchCmd_=0,transferCommandPool_=1,immCommandPool=2,device=3,allocator=4,transferQueue_=5,graphicsQueue=6;
    unsigned batchesSubmitted_=0;
    std::vector<InFlightBatch>inFlightBatches_;
    VkDeviceSize inFlightUploadBytes_=0;
    std::vector<Buffer>batchStagingBuffers_;
    std::vector<Raw>batchRawStaging_;
    void freeRawStaging(){releasedRaw+=batchRawStaging_.size();batchRawStaging_.clear();}
    unsigned waits=0;
    bool waitAllUploads(){++waits;inFlightBatches_.clear();inFlightUploadBytes_=0;return true;}
    void pollUploadBatches(){}
    void beginUploadBatch();
    void finishInterruptedUploadBatch();void finishUploadBatch(bool synchronous);
    void seed(){beginUploadBatch();beginUploadBatch();beginUploadBatch();batchCmd_=42;batchStagingBuffers_.push_back({11});batchRawStaging_.push_back({21,31});}
};
'''
cases=r'''
int main(){
    VkContext ctx;ctx.finishInterruptedUploadBatch();assert(!submitted);
    // Allocation fails at admission, before nesting/state/staging change.
    failAfter=0;
    try{ctx.beginUploadBatch();assert(false);}catch(const std::bad_alloc&){}
    failAfter=-1;
    assert(!ctx.uploadBatchDepth_&&!ctx.inUploadBatch_&&!ctx.batchCmd_&&ctx.inFlightBatches_.empty()&&!submitted);
    ctx.seed();assert(ctx.inFlightBatches_.capacity()>=16&&ctx.uploadBatchDepth_==3);
    // Simulate complete C++ heap exhaustion while recovering an interrupted
    // nested asset upload: the production finish must not call operator new.
    failAfter=0;ctx.finishInterruptedUploadBatch();failAfter=-1;
    assert(!ctx.uploadBatchDepth_&&!ctx.inUploadBatch_&&!ctx.batchCmd_&&ctx.batchStagingBuffers_.empty()&&ctx.batchRawStaging_.empty()&&ctx.inFlightBatches_.size()==1&&submitted==1);
    auto& batch=ctx.inFlightBatches_.front();assert(batch.cmd==42&&batch.fence==99&&batch.stagingBuffers[0].buffer==11&&batch.rawStaging[0].memory==31);
    ctx.finishInterruptedUploadBatch();assert(submitted==1);
    // The 16-batch and 32 MiB staging watermarks still wait before admission;
    // 15 admitted slots remain safe, including the last slot under heap OOM.
    VkContext bounded;bounded.beginUploadBatch();bounded.finishInterruptedUploadBatch();
    bounded.inFlightBatches_.resize(15);bounded.seed();failAfter=0;
    bounded.finishInterruptedUploadBatch();failAfter=-1;
    assert(bounded.inFlightBatches_.size()==16&&bounded.waits==0);
    bounded.beginUploadBatch();assert(bounded.waits==1&&bounded.inFlightBatches_.empty());bounded.finishInterruptedUploadBatch();
    bounded.inFlightBatches_.resize(1);bounded.inFlightBatches_[0].rawStaging.push_back({1,2,32ull*1024*1024});
    bounded.inFlightBatches_[0].stagingBytes=32ull*1024*1024;bounded.inFlightUploadBytes_=32ull*1024*1024;
    bounded.beginUploadBatch();assert(bounded.waits==2&&bounded.inFlightBatches_.empty());bounded.finishInterruptedUploadBatch();
    for(unsigned stage=0;stage<3;++stage){
        VkContext broken;broken.seed();failEnd=stage==0?-4:0;failFence=stage==1?-4:0;failSubmit=stage==2?-4:0;
        bool threw=false;try{broken.finishInterruptedUploadBatch();}catch(const std::runtime_error&){threw=true;}
        assert(threw&&broken.deviceLost_&&!broken.inUploadBatch_&&!broken.uploadBatchDepth_&&!broken.batchCmd_);
        if(stage<2)assert(broken.inFlightBatches_.empty()&&broken.batchStagingBuffers_.empty()&&broken.batchRawStaging_.empty());
        else assert(broken.inFlightBatches_.size()==1&&broken.inFlightBatches_[0].stagingBuffers[0].buffer==11&&broken.inFlightBatches_[0].rawStaging[0].memory==31);
    }
    assert(releasedCommands==2&&releasedBuffers==2&&releasedRaw==2);
    std::puts("PASS: actual batch admission rejects injected reserve OOM without opening; interrupted nested finish submits once under C++ heap exhaustion; 16-batch/32MiB backpressure retained; end/fence/submit failures stop renderer and preserve submitted ownership");
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-upload-interruption-')as temp:
    path=Path(temp)/'test.cpp';path.write_text(prefix+methods+cases);binary=Path(temp)/'test'
    subprocess.run(shlex.split(os.environ.get('CXX','g++'))+['-std=c++20','-O1','-g','-fsanitize=address,undefined',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
