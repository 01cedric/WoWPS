#!/usr/bin/env python3
"""Execute production GLSL algebra; CPU regression test, not a PS4 image test.

the implementation deliberately restores conservative canopy coverage. It does NOT claim
that the earlier small-gap lighting acceptance criteria are now fulfilled.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
def translate(name, start, entry):
    text = (root / 'assets/shaders' / name).read_text()
    text = text[text.index(start):].replace('inout float ', 'float& ')
    text = text.replace('void main()', 'void ' + entry + '()')
    text = text.replace('(v.lightMatrix * vec4(v.camera.xyz,1.0)).xyz', 'vec3(v.lightMatrix * vec4(vec3(v.camera),1.0))')
    text = text.replace('(v.lightMatrix * vec4(ray,0.0)).xyz', 'vec3(v.lightMatrix * vec4(ray,0.0))')
    text = text.replace('shadow.xy = shadow.xy*0.5+0.5;', 'shadow.x = shadow.x*0.5+0.5; shadow.y = shadow.y*0.5+0.5;')
    for before, after in [('gl_FragCoord.xy','vec2(gl_FragCoord)'), ('endpoint.xyz','vec3(endpoint)'),
                           ('v.camera.xyz','vec3(v.camera)'), ('v.sunDirection.xyz','vec3(v.sunDirection)'),
                           ('v.sunColor.rgb','vec3(v.sunColor)'), ('shadow.xy','vec2(shadow)'), ('tap.rgb','vec3(tap)')]:
        text = text.replace(before, after)
    return re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])', r'\1f', text)

march = translate('volumetric.frag.glsl', 'bool clipAxis(', 'march')
composite = translate('volumetric_composite.frag.glsl', 'float volumeDisplayScale(', 'composite')
assert 'float depth = 1.0f;' in march and 'depth = min(depth,' in march
assert 'roundedEyeDepth' not in composite and 'float accepted' not in composite
# Negative control restores precisely the removed the implementation strict acceptance.
old_composite = composite[composite.index('void composite()'):].replace('void composite()', 'void composite_reference()')
needle = 'float w = weight.x*weight.y * max(0.0f,1.0f-relativeError*12.0f);'
assert needle in old_composite
old_composite = old_composite.replace(needle, needle + '\n w *= tap.a <= float(_Float16(eyeDepth)) ? 1.0f : 0.0f;')
preamble = r'''
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace glm;
struct Volume { mat4 inverseRelativeViewProjection, lightMatrix; vec4 camera, sunDirection, sunColor, parameters; } v;
vec4 gl_FragCoord, outColor;
vec2 TexCoord;
const int sceneDepth=0, shadowDepth=1, scattering=2;
ivec2 sceneExtent, volumeExtent;
std::vector<float> depths;
std::vector<vec4> samples;
ivec2 textureSize(int id,int) { return id==scattering ? volumeExtent : sceneExtent; }
vec4 texelFetch(int id,ivec2 p,int) { return id==scattering ? samples[p.y*volumeExtent.x+p.x] : vec4(depths[p.y*sceneExtent.x+p.x]); }
vec4 textureLod(int id,vec2 uv,float) { return id==sceneDepth ? texelFetch(id,clamp(ivec2(uv*vec2(sceneExtent)),ivec2(0),sceneExtent-1),0) : vec4(1); }
void check(bool ok,const char* why) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::abort(); } }
float encode(float eye) { return (1.f-1.f/eye)/.99f; }
void extent(int w,int h) { sceneExtent=ivec2(w,h); volumeExtent=(sceneExtent+3)/4; depths.assign(w*h,encode(20)); samples.assign(volumeExtent.x*volumeExtent.y,vec4(0)); }
'''
tests = r'''
void generate(bool old) {
    for(int y=0;y<volumeExtent.y;++y) for(int x=0;x<volumeExtent.x;++x) {
        if(old) {
            float d=0;
            for(int oy=0;oy<4;++oy) for(int ox=0;ox<4;++ox)
                d=max(d,depths[min(y*4+oy,sceneExtent.y-1)*sceneExtent.x+min(x*4+ox,sceneExtent.x-1)]);
            outColor=vec4(0,0,0,1.f/(1.f-.99f*d));
        } else { gl_FragCoord=vec4(x+.5f,y+.5f,0,1); march(); }
        samples[y*volumeExtent.x+x]=vec4(.2f,.3f,.4f,float(_Float16(outColor.a)));
    }
}
vec3 resolve(int x,int y,bool old=false) { TexCoord=(vec2(x,y)+.5f)/vec2(sceneExtent); if(old) composite_reference(); else composite(); return vec3(outColor); }
int main() {
    v.inverseRelativeViewProjection=mat4(1); v.inverseRelativeViewProjection[2][3]=-.99f;
    v.lightMatrix=mat4(.005f); v.lightMatrix[3][3]=1;
    v.camera=vec4(0,0,0,1); v.sunDirection=vec4(0,0,1,0); v.sunColor=vec4(1);
    v.parameters=vec4(160,.01f,8,.9f);
    unsigned checked=0;
    for(int shape=0;shape<4;++shape) {
        extent(64,64);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
            float px=x+.5f,py=y+.5f;
            float reciprocal=.05f;
            if(shape==1) reciprocal-=.0002f*px;
            if(shape==2) reciprocal-=.0002f*px+.00013f*py;
            if(shape==3) reciprocal-=.000001f*(px*px+py*py);
            depths[y*64+x]=encode(1.f/reciprocal);
        }
        generate(false);
        for(int y=4;y<60;++y) for(int x=4;x<60;++x) {
            check(length(resolve(x,y)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"continuous surface preserves uniform positive scattering without grid holes"); ++checked;
        }
        generate(true); unsigned holes=0;
        for(int y=4;y<60;++y) for(int x=4;x<60;++x) holes+=length(resolve(x,y,true))==0;
        check(shape==0 ? holes==0 : holes>0,"the implementation negative control exposes sloped and curved surface holes");
        std::printf("shape%d: current zero holes; the implementation negative-control holes %u/3136\n",shape,holes);
    }
    for(ivec2 size : {ivec2(1),ivec2(3,7),ivec2(19,13),ivec2(64,37)}) {
        extent(size.x,size.y); generate(false);
        for(int y=0;y<size.y;++y) for(int x=0;x<size.x;++x) {
            check(length(resolve(x,y)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"odd footprint and boundary reconstruction remains uniform"); ++checked;
        }
    }
    extent(32,24);
    for(auto& d:depths) d=encode(80);
    for(int y=0;y<24;y+=4) for(int x=0;x<32;x+=4) depths[y*32+x]=encode(4);
    generate(false);
    for(auto tap:samples) check(std::abs(tap.a-4)<.01f,"nearest endpoint intentionally retained at canopy silhouettes");
    check(length(resolve(1,1))==0,"documented limitation: mixed-tile canopy opening loses distant scattering");
    for(auto& tap:samples) tap=vec4(.2f,.3f,.4f,80);
    check(length(resolve(0,0))==0,"near branch rejects distant background radiance");
    for(auto& tap:samples) tap=vec4(0,0,0,4);
    check(length(resolve(0,0))==0,"zero scattering stays zero at foreground silhouette");
    // Dark doorway frame with a distant bright opening: coarse tiles touching
    // the frame must not transmit the opening's background radiance onto it.
    extent(32,24);
    for(int y=0;y<24;++y) for(int x=0;x<32;++x)
        depths[y*32+x]=encode(x>=9&&x<=22&&y>=5&&y<=20 ? 80.f : 4.f);
    generate(false);
    for(auto& tap:samples) if(tap.a<10) tap=vec4(0,0,0,tap.a);
    unsigned framePixels=0,openingPixels=0;
    for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
        if(!(x>=9&&x<=22&&y>=5&&y<=20)) {
            check(length(resolve(x,y))==0,"distant bright doorway must not halo onto dark foreground frame"); ++framePixels;
        } else if(length(resolve(x,y))>0) ++openingPixels;
    }
    check(openingPixels>0,"large doorway interior retains background scattering");
    // Honest retained limitation: the restored symmetric filter cannot prove
    // geometric occlusion when nearby layers differ by less than 1/12 depth.
    extent(4,4);
    for(auto& tap:samples) tap=vec4(.2f,.3f,.4f,21);
    check(length(resolve(1,1))>0,"document retained near-layer tolerance: depth21 can contribute at depth20");
    std::printf("PASS: %u continuous/odd-footprint pixels; %u dark doorway-frame pixels; slope, curvature, silhouette, canopy-limitation, near-layer limitation and the implementation negative controls. CPU shader algebra only.\n",checked,framePixels);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-volume--') as temporary:
    directory = Path(temporary)
    (directory/'test.cpp').write_text(preamble+march+composite+old_composite+tests)
    flags=['-std=c++17','-O1','-g','-I'+str(root/'extern/glm')]
    if os.environ.get('SANITIZE')=='1': flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX','c++'),*flags,str(directory/'test.cpp'),'-o',str(directory/'test')],check=True)
    environment=dict(os.environ)
    environment.setdefault('ASAN_OPTIONS','detect_leaks=0')
    environment.setdefault('UBSAN_OPTIONS','halt_on_error=1')
    subprocess.run([str(directory/'test')],check=True,env=environment)
