#!/usr/bin/env python3
"""Execute the production WMO setup phase with allocation faults and budget yields."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/rendering/wmo_renderer.cpp').read_text()
start = source.index('    if (!modelData.setupDone) {', source.index('WMORenderer::ModelLoadResult WMORenderer::loadModelIncremental('))
end = source.index('    }  // end one-time setup', start) + len('    }  // end one-time setup')
setup = source[start:end]
prefix = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>
static long failAfter = -1;
void* operator new(size_t n) {
    if (failAfter == 0) throw std::bad_alloc();
    if (failAfter > 0) --failAfter;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
namespace core {
struct Logger {
    static Logger& getInstance() { static Logger l; return l; }
    template<class... T> void debug(T&&...) {}
};
}
struct VkTexture { unsigned id; };
static VkTexture textures[4]{{0},{1},{2},{3}};
struct Material { uint32_t texture1,texture2,texture3,blendMode,flags; };
struct Model {
    std::vector<std::string> textures;
    std::vector<Material> materials;
    std::unordered_map<uint32_t,uint32_t> textureOffsetToIndex;
};
struct ModelData {
    bool setupDone=false;
    size_t nextTextureIndex=0;
    std::vector<VkTexture*> textures;
    std::vector<std::string> textureNames;
    std::vector<uint32_t> materialTextureIndices,materialBlendModes,materialFlags;
};
enum class ModelLoadResult { InProgress,Complete };
struct FakeContext { unsigned ended=0;void endUploadBatch(){++ended;} };
struct Renderer {
    void* assetManager=reinterpret_cast<void*>(1);
    FakeContext context;
    FakeContext* vkCtx_=&context;
    ModelData modelData;
    std::array<unsigned,4> loads{};
    VkTexture* loadTexture(const std::string& name) {
        // A decode can fail after earlier textures committed. The thrown work
        // has no retained GPU owner; successful loads are tracked by source ID.
        std::vector<char> decode(64);
        const size_t id=name[name.size()-5]-'0';
        assert(id<4);++loads[id];return &textures[id];
    }
    ModelLoadResult run(const Model& model, float budgetMs) {
        const auto loadStepStart=std::chrono::steady_clock::now();
'''
suffix = r'''
        return ModelLoadResult::Complete;
    }
};
static void verify(Renderer& renderer,const Model& model) {
    auto& data=renderer.modelData;
    assert(data.setupDone&&data.nextTextureIndex==model.textures.size());
    assert(data.textures.size()==4&&data.textureNames.size()==4);
    for(size_t i=0;i<4;++i){
        std::string lower=model.textures[i];
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});
        assert(data.textures[i]==&textures[i]&&data.textureNames[i]==lower&&renderer.loads[i]==1);
    }
    assert(data.materialTextureIndices.size()==model.materials.size());
    assert(data.materialBlendModes.size()==model.materials.size());
    assert(data.materialFlags.size()==model.materials.size());
    assert((data.materialTextureIndices==std::vector<uint32_t>{3,2,1,0}));
    for(size_t i=0;i<4;++i){assert(data.materialBlendModes[i]==model.materials[i].blendMode);assert(data.materialFlags[i]==model.materials[i].flags);}
}
int main(){
    Model model;
    for(unsigned i=0;i<4;++i){
        model.textures.push_back("WORLD\\WMO\\TEST_TEXTURES\\VERY_LONG_TEXTURE_"+std::to_string(i)+".BLP");
        model.textureOffsetToIndex.emplace(100+i*100,i);
    }
    model.materials={{400,0,0,1,11},{900,300,0,2,12},{900,800,200,3,13},{0,0,0,4,14}};
    unsigned failures=0,partialTextureFailures=0,postTextureFailures=0;
    for(long fail=0;fail<40;++fail){
        Renderer renderer;
        failAfter=fail;bool failed=false;
        try{assert(renderer.run(model,0)==ModelLoadResult::Complete);}catch(const std::bad_alloc&){failed=true;}
        failAfter=-1;
        if(failed){
            ++failures;
            if(renderer.modelData.nextTextureIndex>0&&renderer.modelData.nextTextureIndex<4)++partialTextureFailures;
            if(renderer.modelData.nextTextureIndex==4)++postTextureFailures;
            assert(renderer.run(model,0)==ModelLoadResult::Complete);
        }
        verify(renderer,model);
        // Already-complete setup never loads or appends again.
        assert(renderer.run(model,0)==ModelLoadResult::Complete);verify(renderer,model);
    }
    assert(failures>=10&&partialTextureFailures>0&&postTextureFailures==3);
    Renderer budgeted;
    for(unsigned completed=1;completed<4;++completed){
        assert(budgeted.run(model,0.000000001f)==ModelLoadResult::InProgress);
        assert(budgeted.modelData.nextTextureIndex==completed&&!budgeted.modelData.setupDone);
    }
    assert(budgeted.run(model,0.000000001f)==ModelLoadResult::Complete);verify(budgeted,model);
    assert(budgeted.context.ended==3);
    std::printf("PASS actual WMO setup: %u allocation faults (%u partial-texture, %u material arrays), stable source slots, no repeated committed loads, bounded one-texture yields\n",failures,partialTextureFailures,postTextureFailures);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-wmo-setup-retry-') as temp:
    path = Path(temp) / 'test.cpp'
    path.write_text(prefix + setup + suffix)
    binary = Path(temp) / 'test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'g++')) + [
        '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', str(path), '-o', str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)
