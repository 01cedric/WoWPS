#!/usr/bin/env python3
"""Actual production GLSL via CPU adapter: reconstruction/noise/occlusion tests.
Synthetic evidence only; no PS4 visual or performance claim.
"""
from pathlib import Path
import json, os, subprocess, tempfile
import numpy as np
root=Path(__file__).resolve().parents[2]
fixture=root/'tools/tests/run_volumetric_reconstruction_resolve_tests.py'
ns={'__file__':str(fixture)}
exec(compile(fixture.read_text().split('with tempfile.TemporaryDirectory')[0],str(fixture),'exec'),ns)
# Keep the actual the implementation four-tap resolve as a fixed regression reference. The
# shader bodies, including the new filtered resolve, execute rather than being
# copied into a second Python implementation.
legacy=(root/'tools/tests/fixtures/volumetric_composite_point_fixture.glsl').read_text()
legacy=legacy[legacy.index('float volumeDisplayScale('):]
for name in ('volumeDisplayScale','heightFogOpticalDepth','volumeCoordinate'):
 legacy=legacy.replace(name,'legacy_'+name)
legacy=legacy.replace('void main()', 'void legacyComposite()')
preamble=ns['preamble'].replace('unsigned nearReads=0,farReads=0;', 'unsigned nearReads=0,farReads=0; float fixtureBlocker=.27f;')
preamble=preamble.replace('float d=1;if(closedShadow)', 'float d=1;if(shadowPattern==6)d=fixtureBlocker;else if(closedShadow)')
checks=r'''
void generate(){for(int y=0;y<volumeExtent.y;++y)for(int x=0;x<volumeExtent.x;++x){gl_FragCoord=vec4(x+.5f,y+.5f,0,1);march();samples[y*volumeExtent.x+x]=outColor;}}
vec3 resolve(int x,int y){TexCoord=(vec2(x,y)+.5f)/vec2(sceneExtent);composite();return vec3(outColor);}
vec3 fine(int x,int y){vec2 uv=(vec2(x,y)+.5f)/vec2(sceneExtent);vec3 r=integrateScatteringQuality(uv,depths[y*sceneExtent.x+x],4);return r*volumeDisplayScale(max(r.r,max(r.g,r.b)));}
int main(int argc,char**argv){
 check(argc==2,"output path");FILE*f=fopen(argv[1],"wb");check(f,"output open");
 v.inverseRelativeViewProjection=mat4(1);v.inverseRelativeViewProjection[2][3]=-.99f;
 v.lightMatrix=mat4(.005f);v.lightMatrix[3][3]=1;v.lightMatrix[3][2]=.1f;
 v.nearLightMatrix=v.lightMatrix;v.camera=vec4(0,0,0,1);
 v.sunColor=vec4(1,.65f,.3f,0);v.sunDirection=vec4(0,0,1,4);
 v.parameters=vec4(160,.002f,8,.9f);
 unsigned planePixels=0,layerPixels=0,fallbackPixels=0;
 for(int factor:{2,4}){v.sunDirection.w=float(factor);
  for(int shape=0;shape<3;++shape){extent(32,32);for(int y=0;y<32;++y)for(int x=0;x<32;++x){float inv=.05f;if(shape>0)inv-=.0002f*x;if(shape>1)inv-=.00013f*y;depths[y*32+x]=encode(1/inv);}generate();for(auto&t:samples)t=vec4(.2f,.3f,.4f,t.a);nearReads=farReads=0;for(int y=2;y<30;++y)for(int x=2;x<30;++x){check(length(resolve(x,y)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"normalized constant radiance on projected slopes");++planePixels;}check(nearReads+farReads==0,"continuous planes must use filtered samples without fallback");}
  // Foreground guides are dark; background guides are deliberately overbright.
  // An image-wide blur would leak light onto the foreground edge. Each layer
  // must retain only its own guide values, including close doorway surfaces.
  for(float farDepth:{80.f,4.04f}){extent(32,24);for(int y=0;y<24;++y)for(int x=0;x<32;++x)depths[y*32+x]=encode(x<16?4.f:farDepth);generate();
   for(int sy=0;sy<volumeExtent.y;++sy)for(int sx=0;sx<volumeExtent.x;++sx){int px=std::min(sx*factor+factor/2,31);samples[sy*volumeExtent.x+sx]=vec4(px<16?vec3(.2f):vec3(100.f),0);}
   for(int y=2;y<22;++y)for(int x=2;x<30;++x){nearReads=farReads=0;vec3 r=resolve(x,y);check(nearReads+farReads==0,"per-tap gather avoids fallback at planar layer edges");float expected=x<16?.2f/1.2f:100.f/101.f;check(abs(r.r-expected)<.000002f,"no cross-layer light at walls/near-depth doorway edges");++layerPixels;}}
  // Single-pixel holes missed by every low-resolution source remain an exact
  // endpoint ray. Refinement must not replace these holes with foreground air.
  for(int count:{8,12}){v.parameters.z=float(count);extent(32,24);for(int y=0;y<24;++y)for(int x=0;x<32;++x)depths[y*32+x]=encode(x%4==0&&y%4==0?80.f:4.f);generate();
   for(int y=4;y<20;y+=4)for(int x=4;x<28;x+=4){nearReads=farReads=0;vec3 r=resolve(x,y);check(nearReads==unsigned(count*4)&&farReads==0,"unsupported thin gap uses 32/48 strata, not nine full rays");check(length(r-fine(x,y))<.000001f,"thin gap retains own refined endpoint ray");++fallbackPixels;}
   closedShadow=true;generate();for(int y=0;y<24;++y)for(int x=0;x<32;++x)check(length(resolve(x,y))==0,"closed shadow field remains zero with filtering and fallback");closedShadow=false;
  }
  for(ivec2 sz:{ivec2(1),ivec2(3,7),ivec2(19,13)}){extent(sz.x,sz.y);generate();for(auto&t:samples)t=vec4(.2f,.3f,.4f,0);for(int y=0;y<sz.y;++y)for(int x=0;x<sz.x;++x)check(length(resolve(x,y)-vec3(.2f,.3f,.4f)/1.4f)<.000002f,"odd extents and clamped footprints preserve normalized radiance");}
 }
 // Kernel partitions unity and is continuous when the nearest grid centre
 // changes. Avoid a new grid/checkerboard pattern while smoothing old grain.
 for(int i=0;i<10001;++i){float p=float(i)/10000;float center=floor(p+.5f),sum=0;for(int x=-1;x<=1;++x)sum+=volumeFilterWeight(center+float(x)-p);check(abs(sum-1)<.000001f,"quadratic kernel partitions unity");}
 // Flat blocker fixtures isolate estimator noise from real scene variation.
 v.inverseRelativeViewProjection[0][0]=.001f;v.inverseRelativeViewProjection[1][1]=.001f;
 for(int factor:{2,4}){v.sunDirection.w=float(factor);extent(128,128);for(auto&d:depths)d=encode(80);
  for(int count:{8,12}){v.parameters.z=float(count);shadowPattern=6;
   for(float blocker:{.171f,.223f,.271f,.333f}){fixtureBlocker=blocker;generate();
    for(int y=0;y<128;++y)for(int x=0;x<128;++x){vec2 uv=(vec2(x,y)+.5f)/vec2(sceneExtent);
     nearReads=farReads=0;vec3 raw=integrateScattering(uv,encode(80));check(nearReads==unsigned(count)&&farReads==0,"base march retains 8/12 reads");
     nearReads=farReads=0;vec3 refined=integrateScatteringQuality(uv,encode(80),4);check(nearReads==unsigned(count*4)&&farReads==0,"refinement budget is 32/48 reads");
     TexCoord=uv;legacyComposite();float old=outColor.r;
     nearReads=farReads=0;vec3 filtered=resolve(x,y);check(nearReads+farReads==0,"valid 3x3 gather has no shadow marching cost");
     float row[]={raw.r,refined.r,old,filtered.r};fwrite(row,sizeof(float),4,f);
    }
   }
  }
 }
 fclose(f);printf("PASS %u plane pixels, %u depth-layer pixels, %u thin-hole fallbacks; normalized edges; closed-air zero; 8/12 base and 32/48 fallback steps\n",planePixels,layerPixels,fallbackPixels);
}
'''
out=root/'build-test-results/smoothing';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='wowps-smoothing-') as tmp:
 p=Path(tmp);src=p/'test.cpp';src.write_text(preamble+ns['translate'](ns['common']+ns['march']+ns['comp']+legacy)+checks)
 flags=['-std=c++17','-O1','-g','-I'+str(root/'extern/glm')]
 if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([os.environ.get('CXX','c++'),*flags,str(src),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'samples.bin')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
 samples=np.fromfile(p/'samples.bin',dtype=np.float32).reshape(2,2,4,128,128,4).astype(np.float64)
metrics=[]
for fi,factor in enumerate((2,4)):
 for ci,count in enumerate((8,12)):
  d=samples[fi,ci,:,4:-4,4:-4,:]
  # Variance about each fixture mean; aggregate four blocker positions to avoid
  # selecting a favourable alignment of blocker and sample stratum boundaries.
  variance=d.var(axis=(1,2)).mean(axis=0)
  m={'factor':factor,'base_steps':count,'fallback_steps':count*4,
     'fallback_variance_ratio':float(variance[1]/variance[0]),
     'filtered_variance_ratio':float(variance[3]/variance[2]),
     'fallback_mean_relative_error':float(abs(d[:,:,:,1].mean()/d[:,:,:,0].mean()-1)),
     'filtered_mean_relative_error':float(abs(d[:,:,:,3].mean()/d[:,:,:,2].mean()-1))}
  assert m['fallback_variance_ratio']<.25,m
  assert m['filtered_variance_ratio']<.85,m
  assert m['fallback_mean_relative_error']<.01,m
  assert m['filtered_mean_relative_error']<.01,m
  metrics.append(m);print(json.dumps(m))
(out/'numerical-results.json').write_text(json.dumps({'fixture':'Actual GLSL through CPU adapter; not PS4 appearance/timing validation','results':metrics},indent=2)+'\n')
print('PASS variance reduction and mean-energy preservation; console quality/FPS remain unverified')
