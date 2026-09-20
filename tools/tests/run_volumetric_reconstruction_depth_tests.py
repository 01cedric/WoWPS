#!/usr/bin/env python3
"""Run actual volume/composite GLSL algebra and Camera matrices on the CPU.

No GPU screenshot/performance claim. GLSL translation changes syntax and texture
access only; depth thresholds, footprint mapping and integration stay production.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
# The farthest/strict acceptance criteria below belong to historical the reference.
# Do not report its canopy assertions as passing against the the implementation containment
# rollback: execute the replacement suite, which explicitly tests that loss.
if 'the implementation regression containment' in (root/'assets/shaders/volumetric.frag.glsl').read_text():
    print('SUPERSEDED: the implementation farthest/strict suite; running the implementation rollback and negative-control tests instead.', flush=True)
    import sys
    subprocess.run([sys.executable, str(root/'tools/tests/run_volumetric_reconstruction_edge_tests.py')], check=True)
    raise SystemExit(0)

def translate(name, start, main):
    s = (root / ('assets/shaders/' + name)).read_text()
    s = s[s.index(start):].replace('inout float ', 'float& ')
    s = s.replace('void main()', 'void ' + main + '()')
    s = s.replace('(v.lightMatrix * vec4(v.camera.xyz,1.0)).xyz', 'vec3(v.lightMatrix * vec4(vec3(v.camera),1.0))')
    s = s.replace('(v.lightMatrix * vec4(ray,0.0)).xyz', 'vec3(v.lightMatrix * vec4(ray,0.0))')
    s = s.replace('shadow.xy = shadow.xy*0.5+0.5;', 'shadow.x = shadow.x*0.5+0.5; shadow.y = shadow.y*0.5+0.5;')
    for old, new in [('gl_FragCoord.xy', 'vec2(gl_FragCoord)'), ('endpoint.xyz', 'vec3(endpoint)'),
                     ('v.camera.xyz', 'vec3(v.camera)'), ('v.sunDirection.xyz', 'vec3(v.sunDirection)'),
                     ('v.sunColor.rgb', 'vec3(v.sunColor)'), ('shadow.xy', 'vec2(shadow)'), ('tap.rgb', 'vec3(tap)')]:
        s = s.replace(old, new)
    return re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])', r'\1f', s)

# Extract the actual C++ matrix setup, so this test fails if translation is
# accidentally reintroduced into the inverse used by both shader passes.
setup_source = (root/'src/rendering/post_process_volumetric.inc').read_text()
setup_start = setup_source.index('const glm::mat4 viewRotation =')
setup_end = setup_source.index(';', setup_source.index('data.inverseRelativeViewProjection =', setup_start)) + 1
setup = setup_source[setup_start:setup_end].replace('data.', 'v.')
preamble = r'''
#include "rendering/camera.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>
#include <cstring>
#include <cstdint>
using namespace glm;
struct Volume { mat4 inverseRelativeViewProjection, lightMatrix; vec4 camera, sunDirection, sunColor, parameters; } v;
vec4 gl_FragCoord, outColor;
vec2 TexCoord;
const int sceneDepth=0, shadowDepth=1, scattering=2;
ivec2 sceneExtent, volumeExtent;
std::vector<float> depthPixels;
std::vector<vec4> volumePixels;
int shadowMode=0, shadowReads=0;
ivec2 textureSize(int texture,int) { return texture==scattering ? volumeExtent : sceneExtent; }
vec4 texelFetch(int texture,ivec2 p,int) {
    if(texture==scattering) return volumePixels[p.y*volumeExtent.x+p.x];
    return vec4(depthPixels[p.y*sceneExtent.x+p.x]);
}
vec4 textureLod(int texture,vec2 uv,float) {
    if(texture==sceneDepth) return texelFetch(texture,clamp(ivec2(uv*vec2(sceneExtent)),ivec2(0),sceneExtent-1),0);
    ++shadowReads;
    return vec4(shadowMode==0 ? 1.f : shadowMode==1 ? 0.f : uv.x>.5f ? .2f : 1.f);
}
void require(bool value,const char* message) {
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::abort(); }
}
void extent(ivec2 size) {
    sceneExtent=size; volumeExtent=(size+3)/4;
    depthPixels.assign(size.x*size.y,1);
    volumePixels.assign(volumeExtent.x*volumeExtent.y,vec4(0));
}
float deviceDepth(wowee::rendering::Camera& camera,float distance) {
    vec4 h=camera.getProjectionMatrix()*vec4(0,0,-distance,1);
    return h.z/h.w;
}
void cameraSetup(wowee::rendering::Camera* camera_) {
'''
preamble += setup + '\n}\n'
tests = r'''
void generateVolume(bool nearestFixture=false) {
    for(int y=0;y<volumeExtent.y;++y) for(int x=0;x<volumeExtent.x;++x) {
        gl_FragCoord=vec4(x+.5f,y+.5f,0,1); shadowReads=0; if(nearestFixture) marchNearestFixture(); else march();
        require(shadowReads<=int(v.parameters.z),"fixed quality shadow-read budget");
        outColor.a=float(_Float16(outColor.a));
        volumePixels[y*volumeExtent.x+x]=outColor;
    }
}
vec3 resolve(vec2 uv) {
    TexCoord=uv; composite();
    require(std::isfinite(outColor.r)&&outColor.r>=0&&outColor.r<1,"finite bounded LDR resolve");
    return vec3(outColor);
}
int main() {
    unsigned halfChecks=0;
    for(uint16_t bits=1;bits<0x7bf0;++bits) {
        _Float16 half; std::memcpy(&half,&bits,2); const float h=float(half);
        uint16_t previousBits=bits-1; std::memcpy(&half,&previousBits,2); const float previous=float(half);
        const float midpoint=(h+previous)*.5f;
        for(float input : {std::nextafter(midpoint,0.f),midpoint,std::nextafter(midpoint,65504.f),h}) {
            require(roundedDepthGuide(input)==float(_Float16(input)),"GLSL depth guide equals IEEE half round-to-even at every rounding boundary");
            ++halfChecks;
        }
    }
    unsigned positions=0;
    for(int size=1;size<=257;++size) {
        const int low=(size+3)/4;
        for(int i=0;i<low;++i) {
            const float center=i*4+std::min(4,size-i*4)*.5f;
            require(std::abs(quarterCoordinate(center,size,low)-i)<.00001f,"each actual footprint center maps to its texel");
            ++positions;
        }
        if(low>1) {
            const float previous=(low-2)*4+2;
            require(std::abs(quarterCoordinate(previous-.0001f,size,low)-quarterCoordinate(previous+.0001f,size,low))<.0002f,"odd tail mapping continuous");
        }
    }
    wowee::rendering::Camera camera;
    camera.setAspectRatio(16.f/9); camera.setRotation(123,24); camera.setRoll(.17f);
    v.sunDirection=vec4(camera.getForward(),0); v.sunColor=vec4(.5f,.7f,1,0);
    v.parameters=vec4(160,.008f,12,.9f);
    float legacyDrift=0;
    double legacyReferenceError=0, relativeReferenceError=0;
    unsigned rayChecks=0;
    for(float fov : {45.f,85.f,120.f}) {
        camera.setFov(fov); camera.setJitter(.0002f,-.0003f);
        camera.setPosition(vec3(0)); cameraSetup(&camera);
        const mat4 originInverse=v.inverseRelativeViewProjection;
        for(vec3 position : {vec3(0),vec3(10000,-10000,100),vec3(-18000,13000,100)}) {
            camera.setPosition(position); cameraSetup(&camera);
            const mat4 legacy=inverse(camera.getViewProjectionMatrix());
            const dmat4 reference=inverse(dmat4(camera.getProjectionMatrix())*dmat4(mat4(mat3(camera.getViewMatrix()))));
            for(float z : {.5f,1.f,50.f,160.f,1000.f,30000.f}) {
                float device=deviceDepth(camera,z);
                for(vec2 uv : {vec2(.001f),vec2(.5f),vec2(.999f),vec2(.001f,.999f)}) {
                    vec4 clip(uv*2.f-1.f,device,1);
                    vec4 baseline=originInverse*clip, relative=v.inverseRelativeViewProjection*clip;
                    require(length(relative-baseline)==0,"relative reconstruction invariant under large world translation");
                    require(relative.w>0&&std::isfinite(1/relative.w),"near to far inverse w positive and finite");
                    require(std::abs(1/relative.w-z)<std::max(.0001f,z*.008f),"linear guide agrees with actual perspective camera including far-plane float precision");
                    vec4 old=legacy*clip;
                    const dvec4 expected=reference*dvec4(clip);
                    const dvec3 expectedDelta=dvec3(expected)/expected.w;
                    if(z<=160) {
                        const double newError=length(dvec3(vec3(relative)/relative.w)-expectedDelta);
                        const double oldError=length(dvec3(vec3(old)/old.w-position)-expectedDelta);
                        relativeReferenceError=std::max(relativeReferenceError,newError);
                        legacyReferenceError=std::max(legacyReferenceError,oldError);
                        require(newError<.015,"relative ray matches independent double inverse of actual Camera matrices throughout march range");
                    }
                    if(z==160) legacyDrift=std::max(legacyDrift,length(vec3(old)/old.w-position-vec3(relative)/relative.w));
                    ++rayChecks;
                }
            }
        }
    }
    require(legacyDrift>.2f,"fixture reproduces pre-fix large-world endpoint drift");
    camera.setPosition(vec3(10000,-10000,100)); camera.setRotation(0,0); camera.setRoll(0); camera.setFov(85);
    camera.clearJitter(); cameraSetup(&camera);
    v.camera=vec4(camera.getPosition(),1); v.sunDirection=vec4(camera.getForward(),0);
    // An affine shadow box covering forward air. World eye maps to origin;
    // local forward (world +X) maps to shadow depth z, so depth zero blocks it.
    v.lightMatrix=mat4(0); v.lightMatrix[1][0]=.005f; v.lightMatrix[2][1]=.005f;
    v.lightMatrix[0][2]=.005f; v.lightMatrix[3]=vec4(50,-.5f,-49.9f,1);
    unsigned resolved=0;
    for(ivec2 size : {ivec2(1),ivec2(3,7),ivec2(12,8),ivec2(19,13),ivec2(64,37)}) {
        extent(size);
        for(auto& d:depthPixels) d=deviceDepth(camera,20);
        for(int quality : {8,12}) {
            v.parameters.z=float(quality); shadowMode=0; generateVolume();
            for(const vec4& sample:volumePixels) {
                require(std::abs(sample.a-20)<.001f,"planar guide independent of quarter ray angle");
                require(sample.r>0,"unoccluded air remains visible");
            }
            // Uniform radiance must survive every full-resolution pixel even at
            // steep ray angles and short final quarter footprints.
            for(auto& sample:volumePixels) sample=vec4(.2f,.3f,.4f,sample.a);
            for(int y=0;y<size.y;++y) for(int x=0;x<size.x;++x) {
                const vec3 color=resolve((vec2(x,y)+.5f)/vec2(size));
                require(length(color-vec3(.2f,.3f,.4f)/1.4f)<.000001f,"same-plane guide introduces no false rejection");
                ++resolved;
            }
            // A single foreground pixel cannot inherit bright background taps.
            depthPixels[0]=deviceDepth(camera,2);
            require(length(resolve(vec2(.5f)/vec2(size)))==0,"foreground rejects bright background taps");
            depthPixels[0]=deviceDepth(camera,20);
            // Symmetric rejection remains: foreground coarse rays do not fill
            // background with a mismatched (shortened) scattering result.
            for(auto& sample:volumePixels) sample.a=2;
            require(length(resolve(vec2(.5f)))==0,"background rejects foreground endpoint taps");
            depthPixels[0]=deviceDepth(camera,2); generateVolume();
            require(std::abs(volumePixels[0].a-(size==ivec2(1) ? 2.f : 20.f))<.00001f,"farthest actual source depth retained for gap reconstruction");
            depthPixels[0]=deviceDepth(camera,20); shadowMode=1; generateVolume();
            for(const auto& sample:volumePixels) require(length(vec3(sample))==0,"fully blocked air remains exactly dark");
            require(length(resolve(vec2(.5f)))==0,"fully blocked volume resolves dark");
        }
    }
    // Paired canopy fixture: each 4x4 footprint includes a branch at four
    // yards and an open background at eighty. Old minimum selection removes
    // all background shafts. Farthest actual rays preserve the openings, while
    // near branch pixels still reject every longer ray.
    extent(ivec2(32,24));
    for(auto& d:depthPixels) d=deviceDepth(camera,80);
    for(int y=0;y<sceneExtent.y;y+=4) for(int x=0;x<sceneExtent.x;x+=4)
        depthPixels[y*sceneExtent.x+x]=deviceDepth(camera,4);
    unsigned canopyPixels=0;
    for(int quality : {8,12}) {
        for(auto& d:depthPixels) d=deviceDepth(camera,80);
        for(int y=0;y<sceneExtent.y;y+=4) for(int x=0;x<sceneExtent.x;x+=4)
            depthPixels[y*sceneExtent.x+x]=deviceDepth(camera,4);
        v.parameters.z=float(quality); shadowMode=2;
        generateVolume(true);

        for(int y=2;y<sceneExtent.y;y+=4) for(int x=2;x<sceneExtent.x;x+=4)
            require(length(resolve((vec2(x,y)+.5f)/vec2(sceneExtent)))==0,"minimum footprint fixture erases canopy opening shafts");
        generateVolume();
        for(int y=0;y<sceneExtent.y;y+=4) for(int x=0;x<sceneExtent.x;x+=4) {
            require(length(resolve((vec2(x,y)+.5f)/vec2(sceneExtent)))==0,"branch pixel cannot inherit farthest background scattering");
            const vec3 color=resolve((vec2(x+2,y+2)+.5f)/vec2(sceneExtent));
            require(color.r>0,"farthest real ray retains light through every canopy opening");
            ++canopyPixels;
        }
        const vec3 left=resolve(vec2(.2f,.5f)),right=resolve(vec2(.8f,.5f));
        require(right.r>left.r*1.8f,"composited gap radiance retains blocker-shaped shaft contrast");
        // A closed wall chooses exactly its own real endpoint and never a
        // farther ray; fully shadowed air stays zero after both shader passes.
        for(auto& d:depthPixels) d=deviceDepth(camera,4);
        shadowMode=1; generateVolume();
        for(int y=0;y<sceneExtent.y;++y) for(int x=0;x<sceneExtent.x;++x)
            require(length(resolve((vec2(x,y)+.5f)/vec2(sceneExtent)))==0,"closed shadowed wall has no added scatter");
    }
    std::printf("PASS: %u IEEE-half boundary checks, %u actual footprint centres, %u Camera near/far/angle/translation rays, %u production composite pixels, %u canopy openings; max160yd errors versus double Camera reference old %.3fyd new %.5fyd; bounded8/12 marches, actual farthest endpoints, strict foreground rejection, blocker-shaped shafts and zero blocked air. CPU math only.\n",halfChecks,positions,rayChecks,resolved,canopyPixels,legacyReferenceError,relativeReferenceError);
}
'''
marchSource = translate('volumetric.frag.glsl','bool clipAxis(', 'march')
nearestSource = marchSource[marchSource.index('void march()'):].replace('void march()', 'void marchNearestFixture()').replace('float depth = -1.0f;', 'float depth = 1.0f;').replace('sampleDepth > depth', 'sampleDepth < depth')
source = preamble + marchSource + nearestSource + translate('volumetric_composite.frag.glsl','float volumeDisplayScale(', 'composite') + tests
with tempfile.TemporaryDirectory(prefix='wowps-volume--') as temporary:
    d=Path(temporary); (d/'test.cpp').write_text(source)
    flags=['-std=c++17','-O1','-g','-DGLM_FORCE_DEPTH_ZERO_TO_ONE','-I'+str(root/'extern/glm'),'-I'+str(root/'include')]
    if os.environ.get('SANITIZE')=='1': flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX','c++'),*flags,str(d/'test.cpp'),str(root/'src/rendering/camera.cpp'),'-o',str(d/'test')],check=True)
    environment=dict(os.environ); environment.setdefault('ASAN_OPTIONS','detect_leaks=0'); environment.setdefault('UBSAN_OPTIONS','halt_on_error=1')
    subprocess.run([str(d/'test')],check=True,env=environment)
