#!/usr/bin/env python3
"""Execute actual GLSL scalar/vector light helpers as C++; no GPU image claim."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def function(source, name):
    match = re.search(r'(?:vec3|float) ' + name + r'\([^)]*\)\s*\{', source)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    # GLSL's floating literals are float, while C++ defaults to double.
    body = re.sub(r'([A-Za-z_]\w*\[i\])\.(?:xyz|rgb)', r'vec3(\1)', source[match.start():end])
    return re.sub(r'\b(\d+\.\d*(?:e[+-]?\d+)?|\d+e[+-]?\d+)\b', r'\1f', body)

sources = {name: (ROOT / f'assets/shaders/{name}.frag.glsl').read_text()
           for name in ('terrain', 'm2', 'character')}
cpu = (ROOT / 'src/rendering/character_renderer.cpp').read_text()
assert 'emissiveTint = preservedCandleEmissionTint(' in cpu
assert 'emissiveBoost = 1.0f; // Preserve this named candle' in cpu
assert 'result = characterEmission(texColor.rgb, emissiveTint, emissiveBoost);' in sources['character']
assert sources['m2'].count('directionalSpecular(norm, ldir, viewDir, specularIntensity)') == 1
assert sources['character'].count('directionalSpecular(norm, ldir, viewDir, specularIntensity)') == 1
assert 'texColor.rgb, lightColor.rgb, shadow)' in sources['m2']
assert 'textureLod(uTexture, TexCoord, 4.0).rgb * vec3(tintR, tintG, tintB)' in sources['m2']
assert 'if (unlit == 0) result += localLightContribution' in sources['m2']
assert 'if (unlit == 0) result += localLightContribution' in sources['character']
# Sky, legitimate unlit emission and additive distance attenuation stay separate.
assert 'if (vSkyMode != 0)' in sources['m2'] and 'result *= fogFactor;' in sources['m2']
assert 'result = texColor.rgb * emissiveBoost;' in sources['m2']
assert 'vec3 result = ambient + shadow * diffuse;' in sources['terrain']
assert 'ambientTerm * texColor.rgb' in sources['m2']
assert 'ambientColor.rgb * texColor.rgb' in sources['character']

preamble = '''#include <glm/glm.hpp>
#include "rendering/character_emission.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
using namespace glm;
using std::pow;
float inversesqrt(float x) { return 1.0f / std::sqrt(x); }
'''
units = []
for name, source in sources.items():
    helpers = function(source, 'localLightContribution')
    if name != 'terrain':
        helpers += '\n' + function(source, 'directionalSpecular')
    if name == 'character':
        helpers += '\n' + function(source, 'characterEmission')
    if name == 'm2':
        helpers += '\n' + function(source, 'foliageTransmission')
    units.append('namespace ' + name + ' {\nvec4 localLightPosRadius[64];\n'
                 'vec4 localLightColorIntensity[64];\nivec4 localLightMeta(0);\n' + helpers + '\n}')
main = r'''
bool near(vec3 a, vec3 b) { return length(a-b)<1e-6f; }
int main() {
    vec3 albedo(.2f,.4f,.8f);
    assert(near(character::characterEmission(albedo,vec3(1),1),albedo));
    assert(near(character::characterEmission(albedo,vec3(1),0),vec3(0)));
    for(int i=72; i<=112; ++i) {
        float flicker=float(i)*.01f;
        for(float strength: {1.5f,2.4f}) {
            float oldBoost=strength*flicker;
            vec3 tint(1.28f,1.04f,.82f);
            vec3 preserved=wowee::rendering::preservedCandleEmissionTint(tint,oldBoost);
            assert(near(character::characterEmission(albedo,preserved,1),albedo*(vec3(1)+tint*oldBoost)));
            assert(near(character::characterEmission(vec3(0),preserved,1),vec3(0)));
        }
    }
    vec3 n(0,0,1), front(0,0,1), back(0,0,-1);
    assert(m2::directionalSpecular(n,front,front,0.5f)==0.5f);
    assert(character::directionalSpecular(n,front,front,0.5f)==0.5f);
    // Original Blinn term leaks light across the horizon when eye faces forward.
    vec3 lowBehind = normalize(vec3(1,0,-0.1f));
    assert(pow(max(dot(n,normalize(lowBehind+front)),0.0f),32.0f)>0);
    assert(m2::directionalSpecular(n,lowBehind,front,0.5f)==0);
    assert(character::directionalSpecular(n,front,back,0.5f)==0);
    assert(m2::directionalSpecular(n,front,back,0.5f)==0);
    assert(character::directionalSpecular(n,front,front,0)==0);
    for(int angle=-180; angle<=180; ++angle) {
        float a=float(angle)*3.14159265f/180;
        vec3 light(std::sin(a),0,std::cos(a));
        float m=m2::directionalSpecular(n,light,front,0.5f);
        float c=character::directionalSpecular(n,light,front,0.5f);
        assert(std::isfinite(m) && m==c && m>=0 && m<=0.5f);
        if(light.z<=0) assert(m==0);
    }
    // Cool zone, moonlike night and sunset are synthetic world-UBO inputs.
    for(vec3 key: {vec3(.12f,.20f,.28f),vec3(.02f,.04f,.08f),vec3(.8f,.35f,.1f)}) {
        vec3 lit=m2::foliageTransmission(-1,1,1,vec3(1),key,1);
        assert(near(lit,key*.35f)); // No injected yellow hue.
        assert(near(m2::foliageTransmission(-1,1,1,vec3(1),key,0),vec3(0)));
        assert(near(m2::foliageTransmission(-1,1,1,vec3(0),key,1),vec3(0)));
        assert(near(m2::foliageTransmission(-1,1,1,vec3(1),key,.5f),lit*.5f));
    }
    assert(near(m2::foliageTransmission(1,1,1,vec3(1),vec3(1),1),vec3(0)));
    // Identical radius/color/intensity response across all three actual helpers.
    terrain::localLightMeta.x=m2::localLightMeta.x=character::localLightMeta.x=1;
    terrain::localLightPosRadius[0]=m2::localLightPosRadius[0]=character::localLightPosRadius[0]=vec4(0,0,2,4);
    terrain::localLightColorIntensity[0]=m2::localLightColorIntensity[0]=character::localLightColorIntensity[0]=vec4(1,.4f,.1f,2);
    for(vec3 pos: {vec3(0),vec3(0,0,2),vec3(0,0,6),vec3(0,0,8)}) {
        vec3 a=terrain::localLightContribution(pos,n,vec3(.5f));
        assert(near(a,m2::localLightContribution(pos,n,vec3(.5f))));
        assert(near(a,character::localLightContribution(pos,n,vec3(.5f))));
        assert(std::isfinite(a.x));
        if(pos.z>=6) assert(near(a,vec3(0)));
    }
    assert(near(terrain::localLightContribution(vec3(0),n,vec3(.5f)),vec3(.25f,.1f,.025f)));
    terrain::localLightMeta.x=0;
    assert(near(terrain::localLightContribution(vec3(0),n,vec3(1)),vec3(0)));
    puts("PASS GLSL-extracted surface helpers: neutral/zero emission and 82 candle profile/flicker parity cases; 361 directional angles; backface/opposed eye; cool/night/sunset transmission; shadow/albedo gating; identical local-light radius, center, color and intensity. Source guards: tint, ambient, unlit/sky/additive separation. CPU numerical only; no PS4 pixel/FPS claim.");
}
'''
with tempfile.TemporaryDirectory(prefix='surface--') as temp:
    cpp = Path(temp) / 'surface.cpp'
    cpp.write_text(preamble + '\n'.join(units) + main)
    flags = ['-std=c++20', '-O1', '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'extern/glm')]
    if os.getenv('SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    exe = Path(temp) / 'surface'
    subprocess.run([os.getenv('CXX', 'c++'), *flags, str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
