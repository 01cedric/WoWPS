#!/usr/bin/env python3
"""Execute production GLSL through the existing GLM adapter: all-ray denoise.
Synthetic CPU evidence, not a console render/performance certification.
"""
from pathlib import Path
import ast,json,os,re,subprocess,tempfile
import numpy as np
root=Path(__file__).resolve().parents[2]
adapter=root/'tools/tests/run_volumetric_reconstruction_resolve_tests.py'
tree=ast.parse(adapter.read_text()); ns={'re':re}
for node in tree.body:
 if isinstance(node,ast.FunctionDef) and node.name=='translate':exec(compile(ast.Module(body=[node],type_ignores=[]),str(adapter),'exec'),ns)
 if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='preamble' for t in node.targets):exec(compile(ast.Module(body=[node],type_ignores=[]),str(adapter),'exec'),ns)
preamble=ns['preamble'].replace('unsigned nearReads=0,farReads=0;','unsigned nearReads=0,farReads=0; float fixtureBlocker=.271f;').replace('float d=1;if(closedShadow)','float d=1;if(shadowPattern==6)d=fixtureBlocker;if(closedShadow)')
read=lambda name:(root/'assets/shaders'/name).read_text()
march=read('volumetric.frag.glsl');common=march[march.index('bool clipAxis('):march.index('void main()')]
resolve=read('volumetric_resolve.frag.glsl');assert resolve[resolve.index('bool clipAxis('):resolve.index('// Scene material fog')]==common
resolve=resolve[resolve.index('float volumeCoordinate('):].replace('void main()','void resolveRaw()')
composite=read('volumetric_composite.frag.glsl');composite=composite[composite.index('float volumeDisplayScale('):].replace('void main()','void composite()')
legacy=(root/'tools/tests/fixtures/volumetric_composite_smooth_fixture.glsl').read_text();legacy=legacy[legacy.index('float volumeDisplayScale('):]
for name in ('volumeDisplayScale','heightFogOpticalDepth','volumeCoordinate','volumeFilterWeight'):legacy=legacy.replace(name,'legacy_'+name)
legacy=legacy.replace('void main()','void legacyComposite()')
checks=r'''
void generate(){for(int y=0;y<volumeExtent.y;++y)for(int x=0;x<volumeExtent.x;++x){gl_FragCoord=vec4(x+.5f,y+.5f,0,1);march();samples[y*volumeExtent.x+x]=outColor;}}
void fullResolve(){std::vector<vec4> raw(sceneExtent.x*sceneExtent.y);for(int y=0;y<sceneExtent.y;++y)for(int x=0;x<sceneExtent.x;++x){TexCoord=(vec2(x,y)+.5f)/vec2(sceneExtent);resolveRaw();raw[y*sceneExtent.x+x]=outColor;}volumeExtent=sceneExtent;samples=raw;}
vec3 filtered(int x,int y){return filteredRadiance(ivec2(x,y),sceneExtent,depths[y*sceneExtent.x+x]);}
int main(int argc,char**argv){check(argc==2,"output argument");FILE*f=fopen(argv[1],"wb");check(f,"output file");
 v.inverseRelativeViewProjection=mat4(1);v.inverseRelativeViewProjection[2][3]=-.99f;
 v.lightMatrix=mat4(.005f);v.lightMatrix[3][3]=1;v.lightMatrix[3][2]=.1f;v.nearLightMatrix=v.lightMatrix;
 v.camera=vec4(0,0,0,1);v.sunColor=vec4(1,.65f,.3f,0);v.sunDirection=vec4(0,0,1,2);v.parameters=vec4(160,.002f,8,.9f);
 for(int factor:{2,4}){v.sunDirection.w=float(factor);
 // Constants on flat/slope planes must preserve exact energy and color.
 for(int shape=0;shape<3;++shape){extent(32,24);volumeExtent=sceneExtent;samples.assign(32*24,vec4(.2f,.3f,.4f,0));for(int y=0;y<24;++y)for(int x=0;x<32;++x)depths[y*32+x]=encode(1.f/(.05f-.0002f*x*(shape>0)-.00013f*y*(shape>1)));
 for(int y=0;y<24;++y)for(int x=0;x<32;++x)check(length(filtered(x,y)-vec3(.2f,.3f,.4f))<.000002f,"constant raw radiance on projected planes");}
 // Full-resolution bright sky cannot leak across thin foreground/close doors.
 for(float farDepth:{80.f,4.04f}){extent(32,24);volumeExtent=sceneExtent;samples.resize(32*24);for(int y=0;y<24;++y)for(int x=0;x<32;++x){bool foreground=x<16||x==21;depths[y*32+x]=encode(foreground?4.f:farDepth);samples[y*32+x]=vec4(foreground?vec3(0):vec3(100),0);}
 for(int y=0;y<24;++y)for(int x=0;x<32;++x)check(length(filtered(x,y)-(x<16||x==21?vec3(0):vec3(100)))<.0001f,"no cross-depth light leakage including one-pixel foreground");}
 // Isolated endpoints cannot borrow unsafe light: retain exact center.
 extent(9,9);volumeExtent=sceneExtent;samples.assign(81,vec4(100));depths.assign(81,encode(80));depths[40]=encode(4);samples[40]=vec4(.2f,.3f,.4f,0);check(length(filtered(4,4)-vec3(.2f,.3f,.4f))<.000001f,"isolated endpoint retains safe radiance");
 // Closed shadow field passes the actual reduced march, full resolve and filter.
 extent(32,24);closedShadow=true;generate();fullResolve();for(int y=0;y<24;++y)for(int x=0;x<32;++x)check(length(filtered(x,y))==0,"blocked air stays zero through all three passes");closedShadow=false;
 for(ivec2 size:{ivec2(1),ivec2(3,7),ivec2(19,13)}){extent(size.x,size.y);volumeExtent=sceneExtent;samples.assign(size.x*size.y,vec4(.2f,.3f,.4f,0));for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x)check(length(filtered(x,y)-vec3(.2f,.3f,.4f))<.000002f,"odd extents stable");}
 // Coherent distant layer punctured by leaf endpoints AT every lowres source:
 // old the implementation forced 32/48-step unfiltered fallbacks across the distant layer.
 v.inverseRelativeViewProjection[0][0]=.001f;v.inverseRelativeViewProjection[1][1]=.001f;
 for(int count:{8,12}){v.parameters.z=float(count);
 for(float blocker:{.171f,.223f,.271f,.333f}){extent(128,128);shadowPattern=6;fixtureBlocker=blocker;
 for(int y=0;y<128;++y)for(int x=0;x<128;++x)depths[y*128+x]=encode((x%factor==factor/2&&y%factor==factor/2)?4:80);
 generate();unsigned fallbacks=0;for(int y=4;y<124;++y)for(int x=4;x<124;++x){if(depths[y*128+x]==encode(4))continue;TexCoord=(vec2(x,y)+.5f)/128.f;nearReads=farReads=0;resolveRaw();check(nearReads+farReads==unsigned(count*4),"fixture proves actual exact-fallback path");vec3 raw(outColor);nearReads=farReads=0;legacyComposite();if(nearReads+farReads==unsigned(count*4))check(length(vec3(outColor)-raw*volumeDisplayScale(max(raw.r,max(raw.g,raw.b))))<.000002f,"new resolve preserves old exact ray before denoise");++fallbacks;}check(fallbacks>10000,"substantial fallback fixture");
 fullResolve();nearReads=farReads=0;
 for(int y=4;y<124;++y)for(int x=4;x<124;++x){if(depths[y*128+x]==encode(4))continue;float row[]={samples[y*128+x].r,filtered(x,y).r};fwrite(row,sizeof(float),2,f);}check(nearReads+farReads==0,"final filtering never marches shadows");
 }}shadowPattern=0;v.inverseRelativeViewProjection[0][0]=1;v.inverseRelativeViewProjection[1][1]=1;
 // Debug modes and fog preserve their original output contracts.
 extent(8,8);volumeExtent=sceneExtent;samples.assign(64,vec4(.2f,.3f,.4f,20));TexCoord=vec2(.5f);v.sunColor.w=1;composite();check(abs(outColor.r-.125f)<.00001f,"scene depth diagnostic");v.sunColor.w=2;closedShadow=true;composite();check(length(vec3(outColor))==0,"raw shadow depth diagnostic");closedShadow=false;v.sunColor.w=3;v.fogParameters=vec4(.006f,0,.1f,160);composite();check(length(vec3(outColor)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"volume diagnostic excludes fog");v.sunColor.w=0;v.fogColor=vec4(.3f,.4f,.5f,0);composite();check(outColor.a>=0&&outColor.a<=1,"fog blend coverage bounded");v.fogParameters=vec4(0);
 }
 fclose(f);puts("PASS actual production GLSL: full-resolution fallback filtering, occlusion, blocked zero, constants, odd extents, diagnostics, no extra shadow reads");
}
'''
source=ns['translate'](common+march[march.index('void main()'):].replace('void main()','void march()')+resolve+composite+legacy).replace('center.rgb','vec3(center)')
out=root/'build-test-results/denoise';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='wowps-denoise-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(preamble+source+checks)
 flags=['-std=c++17','-O1','-g','-I'+str(root/'extern/glm')]
 if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CXX','c++'),*flags,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'samples.bin')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
 data=np.fromfile(p/'samples.bin',dtype=np.float32).reshape(-1,2).astype(float)
metrics=[];offset=0
for factor in (2,4):
 n=120*120-(120//factor)**2
 for count in (8,12):
  part=data[offset:offset+4*n].reshape(4,n,2);offset+=4*n
  var=part.var(axis=1).mean(axis=0)
  m={'resolution_factor':factor,'fallback_steps':count*4,'variance_ratio_after_before':float(var[1]/var[0]),'mean_relative_error':float(abs(part[:,:,1].mean()/part[:,:,0].mean()-1))}
  assert m['variance_ratio_after_before']<.3,m
  assert m['mean_relative_error']<.01,m
  metrics.append(m)
assert offset==len(data)
(out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n');print(json.dumps(metrics,indent=2))
print('PASS fallback variance reduced >=70%, mean within1%; console acceptance still required.')
