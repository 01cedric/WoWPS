#!/usr/bin/env python3
"""Execute actual GLSL jitter/march in CPU fixtures; quantify directional bands.

No PS4 image or timing claim. Evidence files are synthetic numerical fixtures.
"""
from pathlib import Path
import json, os, subprocess, tempfile
import numpy as np
root=Path(__file__).resolve().parents[2]
fixture=root/'tools/tests/run_volumetric_reconstruction_resolve_tests.py'
ns={'__file__':str(fixture)}
exec(compile(fixture.read_text().split('with tempfile.TemporaryDirectory')[0],str(fixture),'exec'),ns)
common=ns['common']
assert 'float offset = volumePixelOffset(uv);' in common
assert 'float(seed >> 8u) * (1.0 / 16777216.0)' in common
old=common[common.index('vec3 integrateScattering('):].replace('integrateScattering','oldScattering').replace('volumePixelOffset(uv)','oldOffset(uv)')
old_helper='float oldOffset(vec2 uv) { return fract(52.9829189 * fract(dot(uv*vec2(textureSize(sceneDepth,0)), vec2(0.06711056,0.00583715)))); }\n'
preamble=ns['preamble'].replace('float d=1;if(closedShadow)', 'float d=1;if(shadowPattern==6)d=.27f;else if(closedShadow)')
checks=r'''
int main(int argc,char**argv){
 check(argc==2,"output path");FILE*f=fopen(argv[1],"wb");check(f,"output open");
 v.inverseRelativeViewProjection=mat4(1);v.inverseRelativeViewProjection[2][3]=-.99f;
 // Narrow orthographic-looking patch: the blocker plane intersects almost the
 // same fraction of each ray. Variation should be noise, never diagonal rows.
 v.inverseRelativeViewProjection[0][0]=.001f;v.inverseRelativeViewProjection[1][1]=.001f;
 v.lightMatrix=mat4(.005f);v.lightMatrix[3][3]=1;v.lightMatrix[3][2]=.1f;
 v.nearLightMatrix=v.lightMatrix;v.camera=vec4(0,0,0,1);
 v.sunColor=vec4(1,.65f,.3f,0);v.sunDirection=vec4(0,0,1,4);
 for(int factor:{1,2,4}) {
  extent(256*factor,256*factor);
  for(int count:{8,12}) {
   v.parameters=vec4(160,.002f,float(count),.9f);shadowPattern=6;
   for(int y=0;y<256;++y)for(int x=0;x<256;++x){
    vec2 uv=(vec2(x*factor+factor/2,y*factor+factor/2)+.5f)/vec2(sceneExtent);
    float a=volumePixelOffset(uv),b=volumePixelOffset(uv);
    check(a==b&&a>=0&&a<1,"stable exact seed in [0,1)");
    nearReads=farReads=0;vec3 r=integrateScattering(uv,encode(80));
    check(nearReads==unsigned(count)&&farReads==0,"unchanged shadow read budget");
    vec3 old=oldScattering(uv,encode(80));
    float row[]={oldOffset(uv),a,old.r,r.r};fwrite(row,sizeof(float),4,f);
   }
  }
 }
 fclose(f);
 puts("PASS actual GLSL stable pixel hash, 393216 synthetic shadow rays, unchanged 8/12 shadow reads");
}
'''
out=root/'build-test-results/stripes';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='wowps-striping-') as tmp:
 p=Path(tmp);src=p/'test.cpp';src.write_text(preamble+ns['translate'](common+old_helper+old)+checks)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'extern/glm'),str(src),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'samples.bin')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
 samples=np.fromfile(p/'samples.bin',dtype=np.float32).reshape(3,2,256,256,4).astype(np.float64)
def correlations(a):
 result=[]
 for dy in range(0,13):
  for dx in range(-12,13):
   if dy==0 and dx<=0:continue
   x0=max(0,-dx);x1=min(256,256-dx)
   v=a[:256-dy,x0:x1].ravel();w=a[dy:,x0+dx:x1+dx].ravel()
   result.append((float(np.corrcoef(v,w)[0,1]),dx,dy))
 return max(result,key=lambda row:abs(row[0]))
report=[]
for fi,factor in enumerate((1,2,4)):
 for ci,count in enumerate((8,12)):
  data=samples[fi,ci]
  metrics={'factor':factor,'steps':count,'old_seed_peak_corr':correlations(data[:,:,0]),'new_seed_peak_corr':correlations(data[:,:,1]),'old_radiance_peak_corr':correlations(data[:,:,2]),'new_radiance_peak_corr':correlations(data[:,:,3]),'old_mean_radiance':float(data[:,:,2].mean()),'new_mean_radiance':float(data[:,:,3].mean()),'seed_mean':float(data[:,:,1].mean())}
  assert abs(metrics['new_seed_peak_corr'][0])<.03,metrics
  assert abs(metrics['new_radiance_peak_corr'][0])<.03,metrics
  assert abs(metrics['old_seed_peak_corr'][0])>.5,metrics
  assert abs(metrics['old_radiance_peak_corr'][0])>.5,metrics
  assert abs(metrics['seed_mean']-.5)<.005,metrics
  assert abs(metrics['new_mean_radiance']/metrics['old_mean_radiance']-1)<.01,metrics
  report.append(metrics);print(json.dumps(metrics))
(out/'spatial-correlation.json').write_text(json.dumps({'fixture':'Actual production GLSL in CPU adapter; uniform planar blocker; not console capture','results':report},indent=2)+'\n')
# Scientific visualization of independently generated numeric fixture arrays.
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axes=plt.subplots(2,2,figsize=(10,9))
for ci,count in enumerate((8,12)):
 d=samples[2,ci];lo=min(d[:,:,2].min(),d[:,:,3].min());hi=max(d[:,:,2].max(),d[:,:,3].max())
 for col,label in enumerate(('the implementation linear pixel seed','the implementation avalanche pixel seed')):
  axes[ci,col].imshow(d[:,:,2+col],cmap='inferno',vmin=lo,vmax=hi,interpolation='nearest')
  axes[ci,col].set_title(f'{label}; {count} steps');axes[ci,col].set_axis_off()
fig.suptitle('Synthetic planar occluder: actual GLSL integration\n4× sample spacing; common radiance scale per row; CPU fixture, not PS4')
fig.tight_layout();fig.savefig(out/'synthetic-striping-comparison.png',dpi=140);plt.close(fig)
print('PASS peak short-range correlation < 0.03 at all three pixel spacings; mean energy within 1%; no additional sampling')
