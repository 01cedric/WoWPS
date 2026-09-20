#!/usr/bin/env python3
"""Execute the production shadow fragment body against controlled alpha samples."""
from pathlib import Path
import os, re, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
scene=(root/'assets/shaders/wmo.frag.glsl').read_text()
shadow=(root/'assets/shaders/wmo_shadow.frag.glsl').read_text()
vertex=(root/'assets/shaders/wmo_shadow.vert.glsl').read_text()
header=(root/'include/rendering/wmo_renderer.hpp').read_text()
source=(root/'src/rendering/wmo_renderer.cpp').read_text()
cutoff='if (alphaTest != 0 && texColor.a < 0.5) discard;'
assert cutoff in scene and cutoff in shadow
assert 'TexCoord = aTexCoord;' in vertex
assert 'createPipelineLayout(device, {materialSetLayout_}, {pc})' in source
# Same immutable scene material layout, prefix offsets 0 and 4, shadow set0.
fields=re.search(r'uniform WMOMaterial\s*\{([^}]+)',shadow).group(1)
assert re.findall(r'int\s+(\w+)\s*;',fields)==['hasTexture','alphaTest']
assert 'layout(set = 0, binding = 0)' in shadow and 'layout(set = 0, binding = 1)' in shadow
ubo=header[header.index('struct WMOMaterialUBO {'):header.index('};',header.index('struct WMOMaterialUBO {'))+2]
body=shadow.split('void main() {',1)[1].rsplit('}',1)[0].replace('discard;', 'return false;')
fixture=r'''
#include <glm/glm.hpp>
#include "rendering/wmo_vertex.hpp"
#include "rendering/wmo_shadow_material.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
using vec4=glm::vec4;
static float sampleAlpha=0;
static float gl_FragDepth=1;
static glm::vec4 gl_FragCoord(0,0,0.375f,1);
static int textureSamples=0;
vec4 texture(int,glm::vec2){++textureSamples;return vec4(1,1,1,sampleAlpha);}
int uTexture=0;glm::vec2 TexCoord(0);
'''+ubo+r'''
static_assert(offsetof(WMOMaterialUBO,hasTexture)==0);
static_assert(offsetof(WMOMaterialUBO,alphaTest)==4);
static_assert(wowee::rendering::kWmoShadowVertexAttributes[1].offset==offsetof(wowee::rendering::WMOVertex,texCoord));
bool fragment(int hasTexture,int alphaTest){
'''+body+r'''
return true;
}
int main(){
 for(float alpha:{0.0f,0.49f,0.49999f,0.5f,0.50001f,1.0f}){
  sampleAlpha=alpha;textureSamples=0;
  assert(fragment(1,1)==(alpha>=0.5f));assert(textureSamples==1);
  textureSamples=0;assert(fragment(1,0));assert(textureSamples==0);assert(gl_FragDepth==gl_FragCoord.z);
  assert(fragment(0,1));assert(textureSamples==0); // absent texture uses scene's white fallback
 }
 using namespace wowee::rendering;
 assert(wmoShadowMaterial(false,false)==WMOShadowMaterial::Opaque);
 assert(wmoShadowMaterial(true,false)==WMOShadowMaterial::Cutout);
 assert(wmoShadowMaterial(false,true)==WMOShadowMaterial::None);
 std::puts("PASS: production WMO shadow fragment rejects texture holes, keeps threshold/opaque pixels; same material UBO offsets/base UV; untextured/opaque paths do not sample; authored blend policy");
}
'''
with tempfile.TemporaryDirectory(prefix='wmo-shadow-258-') as t:
 p=Path(t);(p/'test.cpp').write_text(fixture)
 subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-O1','-g','-fsanitize=address,undefined','-I'+str(root/'include'),'-I'+str(root/'extern/glm'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
