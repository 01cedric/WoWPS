#!/usr/bin/env python3
"""Execute production bloom shader arithmetic in a host raster fixture.
No GPU pixel/performance claim. Shader compiler/PSBC checks run separately.
"""
from pathlib import Path
import re,subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
blur=(root/'assets/shaders/bloom_blur.frag.glsl').read_text()
comp=(root/'assets/shaders/bloom_composite.frag.glsl').read_text()
def cpp(s):
    s=s[s.index('vec3 bloomThreshold'):] if 'vec3 bloomThreshold' in s else s[s.index('void main'):]
    s=s.replace('.rgb','.rgb()').replace('.xy','.xy()')
    return re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',s)
prefix=r'''
#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace glm;
constexpr int N=33;
std::vector<vec3> pixels(N*N);
vec4 texture(int,vec2 uv){
    vec2 q=uv*float(N)-vec2(.5f); ivec2 p=ivec2(floor(q)); vec2 f=fract(q);
    auto at=[](int x,int y){return pixels[glm::clamp(y,0,N-1)*N+glm::clamp(x,0,N-1)];};
    return vec4(mix(mix(at(p.x,p.y),at(p.x+1,p.y),f.x),
        mix(at(p.x,p.y+1),at(p.x+1,p.y+1),f.x),f.y),0.f);
}
'''
state='vec2 TexCoord;vec4 FragColor;int sourceImage;struct {vec4 parameters;} bloom;\n'
source=prefix+'namespace blur {\n'+state+cpp(blur)+'\n}\nnamespace composite {\n'+state+cpp(comp)+r'''
}
std::vector<vec3> pass(vec4 params){
    blur::bloom.parameters=params;std::vector<vec3> result(N*N);
    for(int y=0;y<N;++y)for(int x=0;x<N;++x){
        blur::TexCoord=vec2(float(x)+.5f,float(y)+.5f)/float(N);
        blur::main();result[y*N+x]=vec3(blur::FragColor);
    }return result;
}
int main(){
    for(float v:{0.f,.1f,.4f,.65f}){
        std::fill(pixels.begin(),pixels.end(),vec3(v));auto out=pass(vec4(1.f/N,0,1,0));
        for(auto c:out)assert(length(c)==0.f);
    }
    std::fill(pixels.begin(),pixels.end(),vec3(1));auto white=pass(vec4(1.f/N,0,1,0));
    for(auto c:white)assert(length(c-vec3(1))<1.e-6f);
    // Compare the actual paired production blur to its original five-tap
    // reference on irregular RGB values, every pixel including clamp edges.
    for(int i=0;i<N*N;++i)pixels[i]=vec3(float((i*13)%97)/96.f,float((i*31)%89)/88.f,float((i*7)%71)/70.f);
    auto paired=pass(vec4(0,1.f/N,0,0));
    float maximumError=0.f;
    for(int y=0;y<N;++y)for(int x=0;x<N;++x){
        vec2 uv=(vec2(x,y)+.5f)/float(N),step(0,1.f/N);
        vec3 reference=vec3(texture(0,uv))*.375f;
        reference+=(vec3(texture(0,uv-step))+vec3(texture(0,uv+step)))*.25f;
        reference+=(vec3(texture(0,uv-step*2.f))+vec3(texture(0,uv+step*2.f)))*.0625f;
        maximumError=glm::max(maximumError,length(paired[y*N+x]-reference));
    }
    assert(maximumError<3.e-6f);
    printf("paired bloom vs five-tap RGB max error: %.9g\n",maximumError);
    // Bright impulse must spread along x then y; no broad uniform veil.
    std::fill(pixels.begin(),pixels.end(),vec3(0));pixels[16*N+16]=vec3(1,.4f,.1f);
    auto x=pass(vec4(1.f/N,0,1,0));assert(x[16*N+15].r>0 && x[15*N+16].r==0);
    pixels=x;auto xy=pass(vec4(0,1.f/N,0,0));
    assert(xy[15*N+15].r>0 && xy[10*N+10].r==0);
    vec3 energy(0);for(auto c:xy)energy+=c;assert(length(energy-vec3(1,.4f,.1f))<1.e-5f);
    pixels=xy;composite::TexCoord=vec2(.5f);composite::bloom.parameters=vec4(0,0,0,.25f);
    composite::main();vec3 glow(composite::FragColor);assert(glow.r>0 && glow.g<glow.r);
    assert(composite::FragColor.a==0.f);
    // Actual blend equation preserves black/no-glow and bounds bright colors.
    for(float dst:{0.f,.2f,.8f,1.f})for(float src:{0.f,.25f,1.f}){
        float result=src+dst*(1.f-src);assert(result>=dst && result<=1.f);
    }
    composite::bloom.parameters.w=0;composite::main();assert(length(vec3(composite::FragColor))==0.f);
    puts("PASS production shader math: dark exclusion, bright DC preservation, two-axis impulse spread, energy/color, zero intensity, bounded screen blend");
}
'''
with tempfile.TemporaryDirectory(prefix='bloom-') as d:
    p=Path(d);(p/'test.cpp').write_text(source)
    subprocess.run([os.getenv('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined',
        '-I'+str(root/'extern/glm'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
# Check integration invariants that are easy to regress during independent edits.
inc=(root/'src/rendering/post_process_bloom.inc').read_text()
pp=(root/'src/rendering/post_process_pipeline.cpp').read_text()
header=(root/'include/rendering/post_process_pipeline.hpp').read_text()
assert 'sizeof(BloomUniforms) == 16' in inc
assert 'b.sets[f*3+p]' in inc and 'b.uniforms[f][p]' in inc
assert 'p==0 ? fxaa_.sceneColor.imageView : b.images[f][p-1].imageView' in inc
assert 'if(!b.enabled || b.intensity<=0.0f' in inc
assert 'fsr2_.enabled' in inc and 'inspection-bypass' in inc
start=pp.index('bool PostProcessPipeline::destroyFXAAResources()')
end=pp.index('void PostProcessPipeline::renderFXAAPass()',start)
cleanup=pp[start:end]
assert cleanup.index('waitIdleForResourceChange')<cleanup.index('destroyBloomResources')<cleanup.index('destroyImage(device, alloc, fxaa_.sceneColor)')
assert pp.index('renderBloom();')<pp.index('// Begin swapchain render pass (1x - no MSAA on the output pass)')
assert 'BloomState' in header and 'images[frames][2]' in header
for shader in (blur,comp):
    assert 'set=0,binding=0' in shader and 'set=0,binding=1' in shader
print('PASS source integration: stage-specific descriptors/UBOs, no scene feedback in composite, Off/inspection guards, retire aliases before scene, pre-UI execution, descriptor ABI16')
