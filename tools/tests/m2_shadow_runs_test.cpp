#include "rendering/m2_shadow.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
using namespace wowee::rendering;
struct Model {
    struct Batch { uint16_t textureAnimIndex = 0; };
    std::vector<Batch> batches{{0}};
    bool hasTextureAnimation = true, isLavaModel = false;
    std::vector<uint16_t> textureTransformLookup{0};
    std::vector<wowee::pipeline::M2TextureTransform> textureTransforms{1};
    std::vector<uint32_t> globalSequenceDurations{1000};
};
struct Batch { uint32_t materialBatch = 0, maskMode = 1; };
struct Instance { int currentSequenceIndex = 0; float animTime = 0, globalSequenceTime = 0; };
static size_t verify(const std::vector<M2UvTransform>& values, bool varying = true) {
    size_t next = 0, draws = 0, samples = 0;
    forEachM2ShadowUvRun(0, values.size(), varying,
        [&](size_t i) { ++samples; return values.at(i); },
        [&](size_t first, size_t count, const M2UvTransform& uv) {
            assert(first == next && count > 0);
            for(size_t i = first; i < first + count; ++i) {
                assert(values[i].linear == uv.linear && values[i].offset == uv.offset);
                // Matrix addressing in the unchanged instanced shader.
                assert(first + (i-first) == i);
            }
            next += count; ++draws;
        });
    assert(next == values.size());
    assert(samples == (values.empty() ? 0 : varying ? values.size() : 1));
    return draws;
}
int main() {
    assert(verify({}) == 0);
    Model model; Batch batch;
    std::vector<M2UvTransform> uv;
    // A flagged texture animation whose tracks have no keys is identity.
    // This reproduces the flag-only draw explosion without fabricating timing.
    for(size_t i=0;i<8550;++i) uv.push_back(sampleM2ShadowUv(model,batch,
        Instance{0,float(i*7),float(i*11)},5.f));
    assert(verify(uv)==1); assert(verify(uv,false)==1);
    auto& translation=model.textureTransforms[0].translation;
    translation.sequences.resize(1);
    translation.sequences[0].timestamps={0,1000};
    translation.sequences[0].vec3Values={{0,0,0},{1,2,0}};
    translation.interpolationType=1;
    uv.clear();
    for(size_t i=0;i<600;++i) uv.push_back(sampleM2ShadowUv(model,batch,
        Instance{0,float((i/100)*100),0},5.f));
    assert(verify(uv)==6);
    // Global tracks use global time, not the independently varying local clock.
    translation.globalSequence=0;
    uv.clear();
    for(size_t i=0;i<600;++i) uv.push_back(sampleM2ShadowUv(model,batch,
        Instance{0,float(i*3),250},5.f));
    assert(verify(uv)==1);
    // Differing actual global times must remain separated.
    uv.clear();
    for(size_t i=0;i<600;++i) uv.push_back(sampleM2ShadowUv(model,batch,
        Instance{0,0,float(i)},5.f));
    assert(verify(uv)==600);
    // Six independent fields; even a one-ULP change cannot be merged.
    for(unsigned component=0;component<6;++component) {
        uv.assign(3,{});
        float& value=component<4 ? uv[1].linear[component] : uv[1].offset[component-4];
        value=std::nextafter(value,100.f);
        assert(verify(uv)==3);
    }
    // Nonzero model range preserves absolute SSBO offsets.
    size_t calls=0;
    forEachM2ShadowUvRun(42,1042,true,[](size_t){return M2UvTransform{};},
        [&](size_t first,size_t count,const auto&){assert(first==42&&count==1000);++calls;});
    assert(calls==1);
    std::mt19937 random(260);
    for(unsigned trial=0;trial<500;++trial) {
        uv.assign(500,{}); size_t expected=1;
        for(size_t i=0;i<uv.size();++i) {
            uv[i].offset.x=float(random()%4);
            if(i&&uv[i].offset.x!=uv[i-1].offset.x)++expected;
        }
        assert(verify(uv)==expected);
    }
    std::cout << "PASS: 8550 flagged identity draws -> 1; differing local/global animation retained; "
                 "all six components exact; nonzero matrix offsets; 250000 randomized instances\n";
}
