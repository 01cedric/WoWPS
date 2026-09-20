#!/usr/bin/env python3
"""Execute current GLSL through GLM. Tests reconstruction, not PS4 GPU images."""
from pathlib import Path
import re, subprocess, tempfile, os
root=Path(__file__).resolve().parents[2]
def read(name,start):
    s=(root/'assets/shaders'/name).read_text(); return s[s.index(start):]
common=read('volumetric.frag.glsl','bool clipAxis(')
common=common[:common.index('void main()')]
other=read('volumetric_composite.frag.glsl','bool clipAxis(')
assert other[:other.index('// Scene material fog')]==common, 'march/fallback physics must be identical'
march=read('volumetric.frag.glsl','void main()').replace('void main()','void march()')
comp=read('volumetric_composite.frag.glsl','float volumeDisplayScale(').replace('void main()','void composite()')
legacy=comp[comp.index('void composite()'):].replace('void composite()', 'void compositeUnsafe()').replace('compatible ? sum : integrateScattering(uv,depth)','sum')
# Instrument entry without substituting implementation arithmetic.
common=common.replace('vec3 integrateScattering(vec2 uv, float depth) {','vec3 integrateScattering(vec2 uv, float depth) { ++rayCalls;')
def translate(s):
    s=s.replace('inout float ','float& ')
    s=s.replace('(v.lightMatrix * vec4(v.camera.xyz,1.0)).xyz','vec3(v.lightMatrix * vec4(vec3(v.camera),1.0))')
    s=s.replace('(v.lightMatrix * vec4(ray,0.0)).xyz','vec3(v.lightMatrix * vec4(ray,0.0))')
    s=s.replace('(v.nearLightMatrix * vec4(v.camera.xyz,1.0)).xyz','vec3(v.nearLightMatrix * vec4(vec3(v.camera),1.0))')
    s=s.replace('(v.nearLightMatrix * vec4(ray,0.0)).xyz','vec3(v.nearLightMatrix * vec4(ray,0.0))')
    s=s.replace('nearShadow.xy = nearShadow.xy*0.5+0.5;','nearShadow.x=nearShadow.x*0.5+0.5; nearShadow.y=nearShadow.y*0.5+0.5;')
    s=s.replace('nearShadow.xy','vec2(nearShadow)')
    s=s.replace('shadow.xy = shadow.xy*0.5+0.5;','shadow.x=shadow.x*0.5+0.5; shadow.y=shadow.y*0.5+0.5;')
    for a,b in [('gl_FragCoord.xy','vec2(gl_FragCoord)'),('endpoint.xyz','vec3(endpoint)'),('v.sunDirection.xyz','vec3(v.sunDirection)'),('v.sunColor.rgb','vec3(v.sunColor)'),('v.fogColor.rgb','vec3(v.fogColor)'),('shadow.xy','vec2(shadow)'),('tap.rgb','vec3(tap)'),('textureLod(scattering,uv,0.0).rgb','vec3(textureLod(scattering,uv,0.0))')]:s=s.replace(a,b)
    return re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',s)
preamble=r'''
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace glm;
struct Volume {mat4 inverseRelativeViewProjection,lightMatrix;vec4 camera,sunDirection,sunColor,parameters;mat4 nearLightMatrix;vec4 fogParameters,fogColor;}v;
vec4 gl_FragCoord,outColor;vec2 TexCoord;
const int sceneDepth=0,shadowDepth=1,scattering=2;
ivec2 sceneExtent,volumeExtent;std::vector<float> depths;std::vector<vec4> samples;
unsigned rayCalls=0; bool closedShadow=false; int shadowPattern=0;
ivec2 textureSize(int id,int){return id==shadowDepth?ivec2(2048,1024):id==scattering?volumeExtent:sceneExtent;}
vec4 texelFetch(int id,ivec2 p,int){return id==scattering?samples[p.y*volumeExtent.x+p.x]:vec4(depths[p.y*sceneExtent.x+p.x]);}
unsigned nearReads=0,farReads=0;
vec4 textureLod(int id,vec2 uv,float){if(id==shadowDepth){
 bool near=uv.x<.5f;
 if(v.sunColor.w!=2&&(uv.x<0.f||uv.x>=.75f||uv.y<0.f||uv.y>=(near?1.f:.5f))){fprintf(stderr,"FAIL volume read outside atlas tiles\n");abort();}
 if(near){++nearReads;uv/=vec2(.5f,1.f);}else{++farReads;uv=(uv-vec2(.5f,0.f))/vec2(.25f,.5f);}
 float d=1;if(closedShadow)d=0;else if(shadowPattern==1)d=uv.x>.45f&&uv.x<.55f?.04f:1.f;else if(shadowPattern==2)d=(int(floor(uv.x*40))%2)==0?1.f:.04f;else if(shadowPattern==3)d=uv.x>.45f&&uv.x<.55f&&uv.y>.4f&&uv.y<.6f?1.f:0.f;
 else if(shadowPattern==4)d=near&&((int(floor(uv.x*40))%2)==0)?1.f:0.f;
 else if(shadowPattern==5)d=near?0.f:1.f;
 return vec4(d);}return texelFetch(id,clamp(ivec2(uv*vec2(textureSize(id,0))),ivec2(0),textureSize(id,0)-1),0);}

void check(bool c,const char*m){if(!c){fprintf(stderr,"FAIL %s\n",m);abort();}}
float encode(float eye){return (1.f-1.f/eye)/.99f;}
void extent(int w,int h){sceneExtent=ivec2(w,h);int factor=int(v.sunDirection.w);volumeExtent=(sceneExtent+factor-1)/factor;depths.assign(w*h,encode(20));samples.assign(volumeExtent.x*volumeExtent.y,vec4(0));}
'''
tests=r'''
void generate(){for(int y=0;y<volumeExtent.y;++y)for(int x=0;x<volumeExtent.x;++x){gl_FragCoord=vec4(x+.5f,y+.5f,0,1);march();samples[y*volumeExtent.x+x]=vec4(vec3(outColor),float(_Float16(outColor.a)));}}
vec3 resolve(int x,int y){TexCoord=(vec2(x,y)+.5f)/vec2(sceneExtent);composite();return vec3(outColor);}
vec3 exact(int x,int y){vec3 r=integrateScattering((vec2(x,y)+.5f)/vec2(sceneExtent),depths[y*sceneExtent.x+x]);return r*volumeDisplayScale(max(r.r,max(r.g,r.b)));}
int main(){
 v.inverseRelativeViewProjection=mat4(1);v.inverseRelativeViewProjection[2][3]=-.99f;
 v.nearLightMatrix=mat4(1);v.nearLightMatrix[3][0]=3;
 v.lightMatrix=mat4(.005f);v.lightMatrix[3][3]=1;v.lightMatrix[3][2]=.1f;
 v.camera=vec4(0,0,0,1);v.sunDirection=vec4(0,0,1,0);v.sunColor=vec4(1,1,1,0);v.parameters=vec4(160,.008f,8,.9f);
 for(int factor:{2,4}){v.sunDirection.w=float(factor);
 for(int shape=0;shape<3;++shape){extent(64,64);for(int y=0;y<64;++y)for(int x=0;x<64;++x){float inv=.05f;if(shape>0)inv-=.0002f*x;if(shape>1)inv-=.00013f*y;depths[y*64+x]=encode(1/inv);}generate();for(auto&t:samples)t=vec4(.2f,.3f,.4f,t.a);rayCalls=0;for(int y=2;y<62;++y)for(int x=2;x<62;++x)check(length(resolve(x,y)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"continuous projected plane reuses radiance without grid holes");check(rayCalls==0,"continuous slopes must not fall back to full-resolution marching");printf("plane %d: 3600 reused pixels, zero fallback\n",shape);}
 unsigned tested=0, fallback=0, negativeFailures=0;
 for(int quality:{8,12})for(int pattern=0;pattern<3;++pattern){extent(32,24);v.parameters.z=float(quality);
 for(int y=0;y<24;++y)for(int x=0;x<32;++x){float d=20;if(pattern==0)d=(x%4==0&&y%4==0)?80:4;if(pattern==1)d=x<16?8:8.08f;if(pattern==2)d=1.f/(.05f-.00003f*(x*x+y*y));depths[y*32+x]=encode(d);}generate();
 for(int y=0;y<24;++y)for(int x=0;x<32;++x){unsigned before=rayCalls;vec3 r=resolve(x,y);bool used=rayCalls>before;fallback+=used;check(all(greaterThan(r,vec3(0))),"open air has no zero-weight holes");if(used)check(length(r-exact(x,y))<.000001f,"ambiguous pixel equals its own exact ray, no neighbor leakage");++tested;}
 if(pattern==0){for(int y=0;y<24;y+=4)for(int x=0;x<32;x+=4){check(length(resolve(x,y)-exact(x,y))<.000001f,"single pixel canopy openings retained exactly");compositeUnsafe();negativeFailures+=length(vec3(outColor)-exact(x,y))>.00001f;}}
 if(pattern==1){for(int y=0;y<24;++y)for(int x:{16})check(length(resolve(x,y)-exact(x,y))<.000001f,"near-depth doorway edge does not borrow far-layer light");}
 closedShadow=true;generate();for(int y=0;y<24;++y)for(int x=0;x<32;++x)check(length(resolve(x,y))==0,"closed shadowed air has zero scattering");closedShadow=false;
 }
 for(int pattern=0;pattern<3;++pattern){extent(16,16);for(int y=0;y<16;++y)for(int x=0;x<16;++x){float d=20;if(pattern==0)d=x==0?20:21;if(pattern==1)d=x==15?20:21;if(pattern==2)d=x%2==0?4:80;depths[y*16+x]=encode(d);}generate();for(int y=0;y<16;++y){int x=pattern==0?0:pattern==1?15:(factor==4?7:6);if(pattern!=1)check(length(resolve(x,y)-exact(x,y))<.000001f,"image-edge step and one-pixel strips must use own ray");else{vec3 before=resolve(x,y);for(int sy=0;sy<volumeExtent.y;++sy)for(int sx=0;sx<volumeExtent.x-1;++sx)samples[sy*volumeExtent.x+sx]=vec4(100);check(length(resolve(x,y)-before)<.000001f,"right boundary never borrows the other layer");}}}
 for(ivec2 sz:{ivec2(1),ivec2(3,7),ivec2(19,13)}){extent(sz.x,sz.y);generate();for(auto&t:samples)t=vec4(.2f,.3f,.4f,t.a);for(int y=0;y<sz.y;++y)for(int x=0;x<sz.x;++x)check(length(resolve(x,y)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"odd footprints cover edges with no weight loss");}
 check(negativeFailures>0,"negative control must lose canopy openings when exact edge fallback is removed");
 extent(8,8);generate();v.sunColor.w=1;vec3 sceneDebug=resolve(3,3);check(abs(sceneDebug.r-.125f)<.00001f&&sceneDebug.r==sceneDebug.g,"linear depth diagnostic is raw grayscale");v.sunColor.w=2;closedShadow=true;check(length(resolve(3,3))==0,"shadow diagnostic displays raw depth");closedShadow=false;v.sunColor.w=3;for(auto&t:samples)t=vec4(.2f,.3f,.4f,0);check(length(resolve(3,3)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"scattering diagnostic shows only volume buffer");v.sunColor.w=0;
 // Synthetic shadow-depth fields represent a terrain ridge and separated
 // canopy openings; this verifies shadow transport, not terrain rasterization.
 extent(1,1);depths[0]=encode(80);shadowPattern=1;
 v.camera.x=0;generate();check(length(resolve(0,0))==0,"ridge shadow field blocks the complete eye segment");
 v.camera.x=60;generate();check(length(resolve(0,0))>0,"air beyond ridge shadow remains lit");
 shadowPattern=2;unsigned litGaps=0,darkBands=0;
 for(int band=10;band<30;++band){v.camera.x=((float(band)+.5f)/40.f-.5f)*400.f;generate();vec3 r=resolve(0,0);if(band%2==0){check(length(r)>0,"separated canopy gap retains light");++litGaps;}else{check(length(r)==0,"occluded interval separates shafts");++darkBands;}check(length(r-exact(0,0))<.000001f,"map and fallback use same light transform");v.lightMatrix[3][0]=.05f;generate();vec3 shifted=resolve(0,0);check((length(shifted)>0)!=(length(r)>0),"moving directional shadow transform moves separated shafts coherently");check(length(shifted-exact(0,0))<.000001f,"moving light uses same map and exact-ray transform");v.lightMatrix[3][0]=0;}
 check(litGaps==10&&darkBands==10,"ten separate lit gaps and ten blocked intervals");shadowPattern=0;v.camera.x=0;
 // Indoor classification is not used by the marcher. A closed directional
 // shadow field contributes zero; a bounded opening admits only its own light.
 // This is a synthetic shadow-map/scene-depth test, not WMO raster validation.
 for(int quality:{8,12}){v.parameters.z=float(quality);extent(1,1);depths[0]=encode(80);
 shadowPattern=3;v.camera.x=0;closedShadow=true;generate();check(length(resolve(0,0))==0,"closed room has zero directional scattering");
 closedShadow=false;generate();check(length(resolve(0,0))>0,"open window admits a shadowed light shaft");
 v.camera.x=60;generate();check(length(resolve(0,0))==0,"opaque wall alongside window blocks scattering");
 v.camera.x=0;shadowPattern=0;}
 printf("PASS indoor closed room, open window, wall beside window at both march qualities\n");
 // Near-map-only narrow gaps are deliberately absent from the far map.
 // The real shader must retain these gaps without sampling far in its interior.
 v.nearLightMatrix=v.lightMatrix;shadowPattern=4;
 for(int quality:{8,12}){v.parameters.z=float(quality);extent(1,1);depths[0]=encode(80);
 for(int band=10;band<30;++band){v.camera.x=((float(band)+.5f)/40.f-.5f)*400.f;nearReads=farReads=0;generate();
 check((length(resolve(0,0))>0)==(band%2==0),"fine near cascade preserves gaps absent in far shadow map");
 check(nearReads==unsigned(quality)&&farReads==0,"near interior uses exactly one shadow read per step");}
 v.camera.x=0;closedShadow=true;generate();check(length(resolve(0,0))==0,"blocked near and far remain zero");closedShadow=false;
 // Near blockers must override a fully lit far tile. Its boundary fades
 // continuously toward far coverage, without sampling unused atlas pixels.
 shadowPattern=5;v.nearLightMatrix[0][0]=.01f;v.camera.x=0;generate();check(length(resolve(0,0))==0,"near blocker overrides lit coarse map");
 v.camera.x=98;nearReads=farReads=0;generate();vec3 edge=resolve(0,0);check(length(edge)>0,"cascade transition blends towards far");check(nearReads==unsigned(quality)&&farReads==unsigned(quality),"only transition doubles shadow reads");
 v.camera.x=110;nearReads=farReads=0;generate();check(length(resolve(0,0))>length(edge),"far map takes over outside near cascade");check(nearReads==0&&farReads==unsigned(quality),"outside near uses only far reads");shadowPattern=4;v.nearLightMatrix=v.lightMatrix;}
 v.nearLightMatrix=mat4(1);v.nearLightMatrix[3][0]=3;shadowPattern=0;v.camera.x=0;
 printf("PASS near/far atlas: fine gaps, zero blocked rays, matching edge blend, bounded 8/12 reads except transition\n");

 double energy=0;const int n=100000;for(int i=0;i<n;++i)energy+=directionalScatteringScale(-1.f+2.f*(i+.5f)/n,1.f)*2.f/n*6.2831853f;check(abs(energy-3.14159265)<.002,"phase is normalized under authored Lambert convention");
 float forward=directionalScatteringScale(1,.9f),side=directionalScatteringScale(0,.9f);check(side>0&&side/forward<.02f,"forward phase retains nonzero off-axis light without broad veiling lobe");check(volumeDisplayScale(1000)*1000<1,"LDR scattering bounded");
 printf("PASS %u layered/curved pixels, %u exact fallback; close doorway, canopy holes, odd extents, phase normalization. CPU GLSL only.\n",tested,fallback);}
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-volume-')as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(preamble+translate(common+march+comp+legacy)+tests)
 flags=['-std=c++17','-O1','-g','-I'+str(root/'extern/glm')]
 if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CXX','c++'),*flags,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
