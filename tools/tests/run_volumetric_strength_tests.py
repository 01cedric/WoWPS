#!/usr/bin/env python3
"""Actual GLSL phase/composite regression; controlled inputs, no GPU image claims."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]

def function(path, name):
    source = (ROOT / path).read_text()
    start = source.index('float ' + name + '(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

phase = function('assets/shaders/volumetric.frag.glsl', 'directionalScatteringScale')
composite = function('assets/shaders/volumetric_composite.frag.glsl', 'volumeDisplayScale')
preamble = '''#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
float clamp(float x,float a,float b) { return std::clamp(x,a,b); }
float max(float a,float b) { return std::max(a,b); }
using std::pow;
'''
tests = r'''
float oldPhase(float mu) {
    const float g=.65f;
    return .9f*(1-g*g)/(4*pow(1+g*g-2*g*mu,1.5f));
}
float display(float x) { return x*volumeDisplayScale(x); }
int main() {
    float integral=1-std::exp(-.008f*160);
    const float pi=3.14159265f;
    for(float angle: {0.f,5.f,10.f,15.f,20.f,30.f,45.f,90.f,180.f}) {
        float mu=std::cos(angle*pi/180);
        float before=oldPhase(mu), after=directionalScatteringScale(mu,.9f);
        float ratio=after/before;
        if(angle<=10) assert(ratio>2);
        if(angle==15) assert(ratio>1.4f);
        if(angle==20) assert(ratio>1.1f);
        if(angle>=30) assert(ratio<1);
        std::printf("angle=%5.1f deg raw_gain=%.4f display_scatter(peak_key=.5,range=160m): %.4f -> %.4f\n",
                    angle,ratio,display(.5f*integral*before),display(.5f*integral*after));
    }
    // Compare both strengths after the actual bounded LDR mapping, over
    // synthetic cool/day/night colors, partial visibility and weather strength.
    for(float peak: {.03f,.1f,.5f,1.f})
    for(float visibility: {0.f,.1f,.5f,1.f})
    for(float weather: {0.f,.3f,1.f}) {
        float radiance=peak*visibility*weather*integral*directionalScatteringScale(1,.9f);
        float oldRadiance=peak*visibility*weather*integral*oldPhase(1);
        float mapped=display(radiance);
        assert(std::isfinite(mapped) && mapped>=0 && mapped<1);
        if(visibility==0 || weather==0) assert(mapped==0);
        else assert(mapped>display(oldRadiance));
        // Common scalar tone mapping retains any supplied cool/warm ratio.
        for(float colorRatio: {.1f,.4f,.8f,1.f}) {
            float channel=radiance*colorRatio*volumeDisplayScale(radiance);
            if(mapped>0) assert(std::abs(channel/mapped-colorRatio)<1e-6f);
        }
        for(float scene: {0.f,.1f,.5f,.95f,1.f}) {
            float output=mapped+scene*(1-mapped);
            assert(output>=scene-1e-6f && output<=1);
            if(radiance==0) assert(output==scene);
        }
    }
    puts("PASS stronger forward cone through 20deg, reduced off-axis wash, bounded composite, color ratios, blocked/zero-weather identity; synthetic CPU regression only.");
}
'''
with tempfile.TemporaryDirectory(prefix='rays--') as temp:
    cpp = Path(temp) / 'rays.cpp'
    cpp.write_text(preamble + phase + composite + tests)
    exe = Path(temp) / 'rays'
    subprocess.run([os.getenv('CXX', 'c++'), '-std=c++17', '-O1', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
