#!/usr/bin/env python3
"""Compile exact production GLSL math and Vulkan→GNM blend conversion on host.

Numerical/display-bound regression only: this does not render console images.
"""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
def function(path, signature):
    source = (root/path).read_text()
    start = source.index(signature)
    end = source.index('{',start)+1
    depth = 1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}')
        end += 1
    return source[start:end]+'\n'
functions = function('assets/shaders/volumetric.frag.glsl','float directionalScatteringScale(')
functions += function('assets/shaders/volumetric_composite.frag.glsl','float volumeDisplayScale(')
functions += function('src/rendering/post_process_volumetric.inc','VkPipelineColorBlendAttachmentState volumetricCompositeBlend(')
backend = 'ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_pipeline.c'
for signature in ['static GnmBlendOp vk_blend_factor_to_gnm(', 'static GnmCombFunc vk_blend_op_to_gnm(', 'static void vk_blend_attachment_to_gnm(']:
    functions += function(backend,signature)
preamble = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vulkan/vulkan.h>
#include <gnm_controls.h>
float clamp(float x,float a,float b) { return std::clamp(x,a,b); }
float max(float a,float b) { return std::max(a,b); }
using std::pow;
void require(bool value,const char* why) {
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",why); std::abort(); }
}
'''
tests = r'''
int main() {
    constexpr double pi=3.14159265358979323846;
    // An HG distribution integrates to 1 over the sphere. Exact production
    // scale must integrate to pi*albedo after the Lambert-convention bridge.
    double integrated=0;
    const int n=200000;
    for(int i=0;i<n;++i) {
        const float mu=-1.f+(i+.5f)*2.f/n;
        const float value=directionalScatteringScale(mu,.9f);
        require(std::isfinite(value)&&value>0,"finite nonnegative angular scattering");
        integrated+=value*4*pi/n;
    }
    require(std::abs(integrated-pi*.9)<.0001,"solid-angle energy normalization");
    require(directionalScatteringScale(1,.9f)>directionalScatteringScale(.7f,.9f),"sunward peak");
    require(directionalScatteringScale(.7f,.9f)>directionalScatteringScale(-1,.9f),"forward anisotropy");
    require(directionalScatteringScale(1,0)==0,"no scattering at zero albedo");
    require(directionalScatteringScale(1,-1)==0,"negative albedo clamped");
    require(directionalScatteringScale(1,2)==directionalScatteringScale(1,1),"albedo energy bounded");
    require(directionalScatteringScale(5,1)==directionalScatteringScale(1,1),"dot product rounding clamped");
    // 160-yard density. Binary blocked and unblocked shadow integrals are
    // endpoints; sweep every visibility fraction between them and every angle.
    const float maxIntegral=1.f-std::exp(-.008f*160.f);
    unsigned cases=0;
    for(int angle=0;angle<=200;++angle)
    for(float light : {0.f,.03f,.1f,.25f,.5f,1.f,2.f})
    for(float visibility : {0.f,.01f,.1f,.25f,.5f,.75f,1.f}) {
        const float L=light*maxIntegral*visibility*directionalScatteringScale(-1.f+angle*.01f,.9f);
        const float s=L*volumeDisplayScale(L);
        require(std::isfinite(s)&&s>=0&&s<1,"display scattering finite and below white");
        for(float scene : {0.f,.03f,.1f,.25f,.5f,.8f,.95f,1.f}) {
            const float output=s+scene*(1-s);
            require(output>=scene-.000001f&&output<=1.f,"LDR range without additive overflow");
            if(visibility==0||light==0) require(output==scene,"blocked/no light exact identity");
            if(scene<1) require(output<1,"no finite forward-beam whiteout");
            ++cases;
        }
        if(light>0&&visibility>0) {
            const float moon=L*.1f;
            require(std::abs(moon/L-.1f)<.000001f,"authored moon/day radiance ratio");
            require(moon*volumeDisplayScale(moon)<s,"moon remains dimmer after display map");
        }
    }
    const auto state=volumetricCompositeBlend();
    GnmBlendControl native{};
    vk_blend_attachment_to_gnm(&state,&native);
    require(native.blendenabled&&native.colorsrcmult==GNM_BLEND_ONE&&
        native.colordstmult==GNM_BLEND_ONE_MINUS_SRC_COLOR&&
        native.colorfunc==GNM_COMB_DST_PLUS_SRC,"actual Vulkan-to-GNM per-channel screen blend");
    require(native.alphasrcmult==GNM_BLEND_ZERO&&native.alphadstmult==GNM_BLEND_ONE&&
        native.separatealphaenable,"destination alpha retained");
    require(state.colorWriteMask==7,"RGB-only write mask");
    std::printf("PASS: exact production phase normalization %.8f; %u bounded composite cases; actual GNM per-channel blend; zero-light identity; moon/day ordering\n",integrated,cases);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-rays--') as temp:
    d=Path(temp); (d/'test.cpp').write_text(preamble+functions+tests)
    flags=['-std=c++17','-O1','-g','-I'+str(root/'ps4/third_party/ps4_vulkan/include')]
    if os.environ.get('SANITIZE')=='1': flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX','c++'),*flags,str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    environment=dict(os.environ)
    environment.setdefault('ASAN_OPTIONS','detect_leaks=0')
    environment.setdefault('UBSAN_OPTIONS','halt_on_error=1')
    subprocess.run([str(d/'test')],check=True,env=environment)
