#!/usr/bin/env python3
"""Execute production ray marcher with deterministic depth textures on the CPU.

This checks shader algebra, occlusion and endpoints, not GPU image quality/FPS.
The production main is translated only for GLSL/C++ syntax and texture access.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'assets/shaders/volumetric.frag.glsl').read_text()
source = source[source.index('bool clipAxis('):]
source = source.replace('inout float ', 'float& ')
source = source.replace('void main()', 'void march()')
# GLSL swizzle reads/writes do not use the same syntax as host GLM.
source = source.replace('endpoint.xyz /= max(endpoint.w, 0.000001);',
                        'endpoint = vec4(vec3(endpoint) / max(endpoint.w, 0.000001), endpoint.w);')
source = source.replace('(v.lightMatrix * vec4(v.camera.xyz,1.0)).xyz',
                        'vec3(v.lightMatrix * vec4(vec3(v.camera),1.0))')
source = source.replace('(v.lightMatrix * vec4(ray,0.0)).xyz',
                        'vec3(v.lightMatrix * vec4(ray,0.0))')
source = source.replace('shadow.xy = shadow.xy*0.5+0.5;',
                        'shadow.x = shadow.x*0.5+0.5; shadow.y = shadow.y*0.5+0.5;')
for old, new in [('gl_FragCoord.xy', 'vec2(gl_FragCoord)'), ('v.camera.xyz', 'vec3(v.camera)'),
                 ('endpoint.xyz', 'vec3(endpoint)'), ('v.sunDirection.xyz', 'vec3(v.sunDirection)'),
                 ('v.sunColor.rgb', 'vec3(v.sunColor)'), ('shadow.xy', 'vec2(shadow)')]:
    source = source.replace(old, new)
# GLSL treats unsuffixed decimal literals as float; C++ otherwise promotes vec3
# expressions to double and GLM intentionally rejects mixed scalar types.
import re
source = re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])', r'\1f', source)
preamble = r'''
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace glm;
struct Volume { mat4 inverseRelativeViewProjection, lightMatrix; vec4 camera, sunDirection, sunColor, parameters; } v;
vec4 gl_FragCoord, outColor;
vec2 TexCoord;
const int sceneDepth=0, shadowDepth=1;
float surface=1.f;
int shadowMode=0;
ivec2 textureSize(int,int) { return ivec2(64); }
vec4 texelFetch(int,ivec2,int) { return vec4(surface); }
vec4 textureLod(int,vec2 uv,float) {
    // Depth 1: clear sky. Depth 0: fully blocked. Stripe: finite building/tree.
    return vec4(shadowMode==0 ? 1.f : shadowMode==1 ? 0.f : uv.x>.55f ? .05f : 1.f);
}
void require(bool value,const char* message) {
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::abort(); }
}
'''
tests = r'''
float run(int mode,float endpoint,int pixel,int quality) {
    surface=endpoint; shadowMode=mode;
    gl_FragCoord=vec4(float(pixel)+.5f,8.5f,0,1);
    v.parameters.z=float(quality);
    march();
    require(std::isfinite(outColor.r)&&outColor.r>=0,"finite nonnegative scattered air");
    return outColor.r;
}
int main() {
    v.inverseRelativeViewProjection=scale(mat4(1),vec3(100));
    v.lightMatrix=scale(mat4(1),vec3(.01f));
    v.camera=vec4(0,0,0,1); v.sunDirection=vec4(0,0,1,0);
    v.sunColor=vec4(.5f,.7f,1,0); v.parameters=vec4(160,.008f,12,.9f);
    for(int quality : {8,12}) {
        const float lit=run(0,1,8,quality);
        require(lit>0,"unblocked air integrates light");
        require(run(1,1,8,quality)==0,"fully shadowed air contributes exactly zero");
        const float gap=run(2,1,8,quality);
        require(std::abs(gap-lit)<1e-6f,"gap between blockers retains unblocked air");
        const float beside=run(0,1,12,quality);
        require(run(2,1,12,quality)<beside*.5f,"occluder creates spatial shaft contrast");
        require(run(0,.1f,8,quality)<lit,"opaque endpoint truncates scattering");
        require(outColor.a==1.f,"reciprocal homogeneous w retained as guide (synthetic affine fixture)");
        run(0,1,8,quality);
        require(std::abs(outColor.b/outColor.r-2.f)<1e-6f,"regional authored hue retained");
        v.lightMatrix[3][0]=3;
        require(run(0,1,8,quality)==0,"outside finite shadow volume invents no light");
        v.lightMatrix[3][0]=0;
    }
    // Compare old per-step transform against reused affine ray coordinates for
    // many world positions/directions and the entire 160-yard march interval.
    unsigned checked=0;
    for(int angle=0;angle<31;++angle) {
        const float a=angle*.19f;
        const vec3 eye(2000+angle*17, -500+angle*7, 70+angle);
        const mat4 light=ortho(-80.f,80.f,-80.f,80.f,.1f,300.f)*
            lookAt(eye+vec3(std::cos(a)*100,std::sin(a)*100,120),eye,vec3(0,0,1));
        const vec3 origin=vec3(light*vec4(eye,1));
        for(int direction=0;direction<47;++direction) {
            const vec3 ray=normalize(vec3(std::cos(direction*.31f),std::sin(direction*.31f),.4f));
            const vec3 transformedRay=vec3(light*vec4(ray,0));
            for(int step=0;step<=160;++step) {
                const vec3 before=vec3(light*vec4(eye+ray*float(step),1));
                const vec3 after=origin+transformedRay*float(step);
                require(length(before-after)<1e-5f,"affine sample coordinates preserved within float precision");
                ++checked;
            }
        }
    }
    std::printf("PASS: production marcher 8/12 samples: blocked/unblocked air, occluder gap contrast, opaque endpoints, authored hue, finite shadow volume; %u affine-coordinate comparisons. CPU math only, no GPU quality/FPS claim.\n",checked);
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-volume--') as temporary:
    d = Path(temporary)
    (d/'test.cpp').write_text(preamble+source+tests)
    flags=['-std=c++17','-O1','-g','-I'+str(root/'extern/glm')]
    if os.environ.get('SANITIZE')=='1': flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run([os.environ.get('CXX','c++'),*flags,str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    environment=dict(os.environ)
    environment.setdefault('ASAN_OPTIONS','detect_leaks=0')
    environment.setdefault('UBSAN_OPTIONS','halt_on_error=1')
    subprocess.run([str(d/'test')],check=True,env=environment)
