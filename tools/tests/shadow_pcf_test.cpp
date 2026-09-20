// Runs the production GLSL helper translated only for C++ syntax against an
// independent nine-tap bilinear-depth-compare oracle. No real GPU is simulated.
#include <glm/glm.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
using glm::vec2;
using glm::vec3;
using glm::floor;
struct ShadowMap { int size, borderMode, impulse; bool quantized; };
using sampler2DShadow = ShadowMap*;
static double depth(const ShadowMap& map, int x, int y) {
    if (map.borderMode == 0 && (x < 0 || y < 0 || x >= map.size || y >= map.size)) return 1.0;
    x=std::clamp(x,0,map.size-1); y=std::clamp(y,0,map.size-1);
    if (map.impulse >= 0) return (x==2+map.impulse%4 && y==2+map.impulse/4) ? 1.0 : 0.0;
    const uint32_t hash=(uint32_t(x)*2654435761u) ^ (uint32_t(y)*2246822519u);
    return double(hash & 65535u)/65535.0;
}
static double bilinearCompare(const ShadowMap& map, double u, double v, double reference) {
    const double px=u*map.size-0.5, py=v*map.size-0.5;
    const int x=int(std::floor(px)), y=int(std::floor(py));
    double fx=px-x, fy=py-y;
    // Sensitivity experiment, not a claim about PS4 sampler precision.
    if (map.quantized) { fx=std::round(fx*256.0)/256.0; fy=std::round(fy*256.0)/256.0; }
    const double a=reference<=depth(map,x,y), b=reference<=depth(map,x+1,y);
    const double c=reference<=depth(map,x,y+1), d=reference<=depth(map,x+1,y+1);
    return (a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy;
}
static float texture(sampler2DShadow map, vec3 coordinates) {
    return float(bilinearCompare(*map, coordinates.x, coordinates.y, coordinates.z));
}
#include "production_pcf.inc"
static double nineTap(const ShadowMap& map, vec3 coordinate) {
    double total=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        total+=bilinearCompare(map,double(coordinate.x)+double(x)/map.size,
                                    double(coordinate.y)+double(y)/map.size,coordinate.z);
    return total/9;
}
int main() {
    std::mt19937 random(254);
    std::uniform_real_distribution<float> uv(-0.002f,1.002f), z(-0.1f,1.1f);
    double maxFloatError=0,maxQuantizedError=0;
    uint64_t cases=0;
    for(int size:{8,1024,2048,4096}) for(int border:{0,1}) for(bool quantized:{false,true}) {
        ShadowMap map{size,border,-1,quantized};
        for(int sample=0;sample<20000;++sample) {
            vec3 c(uv(random),uv(random),z(random));
            // Deliberately cover texel centers, map corners and all border footprints.
            if(sample<1000) {
                c.x=float(sample%20-10)*0.125f/size;
                c.y=float(sample/20%20-10)*0.125f/size;
                if(sample&1)c.x+=1; if(sample&2)c.y+=1;
            }
            const double result=sampleShadowPCF(&map,c,1.0f/size), expected=nineTap(map,c);
            const double error=std::abs(result-expected);
            (quantized ? maxQuantizedError : maxFloatError)=std::max(error,quantized ? maxQuantizedError : maxFloatError);
            assert(result>=-1e-6 && result<=1.000001);
            assert(error < (quantized ? 0.003 : 0.0002));
            ++cases;
        }
    }
    // All sixteen basis impulses prove the entire 4x4 footprint's weights at
    // independent subtexel phases, including the degenerate center phase.
    for(int impulse=0;impulse<16;++impulse) for(int x=0;x<=32;++x) for(int y=0;y<=32;++y) {
        ShadowMap map{8,0,impulse,false};
        vec3 c((3.5f+x/32.0f)/8,(3.5f+y/32.0f)/8,0.5f);
        assert(std::abs(sampleShadowPCF(&map,c,0.125f)-nineTap(map,c)) < 0.000001);
        ++cases;
    }
    std::printf("PASS production PCF: %llu cases; 9->4 comparison samples; max float error %.9g; 8-bit sampler-fraction sensitivity %.9g\n",
        (unsigned long long)cases,maxFloatError,maxQuantizedError);
}
