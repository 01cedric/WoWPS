#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace wowee::ui {
// Normalized bounds of nontransparent source pixels. Kept with small UI art,
// never with a second decoded image or a GPU readback.
struct TextureContentBounds {
    float left=0, right=1, top=0, bottom=1;
    bool valid=false;
};
inline TextureContentBounds textureContentBounds(std::span<const uint8_t> rgba,
                                                uint32_t width,uint32_t height) {
    TextureContentBounds out;
    if(!width || !height || uint64_t(width)*height>rgba.size()/4)return out;
    uint32_t left=width,top=height,right=0,bottom=0;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
        if(!rgba[(size_t(y)*width+x)*4+3])continue;
        left=std::min(left,x);top=std::min(top,y);
        right=std::max(right,x+1);bottom=std::max(bottom,y+1);
    }
    if(left>=right || top>=bottom)return out;
    return {float(left)/width,float(right)/width,float(top)/height,float(bottom)/height,true};
}
// Source UV bounds into the fraction of a cropped/flipped texture's rectangle.
inline bool contentAxis(float first,float last,float low,float high,float& a,float& b) {
    if(first==last)return false;
    a=(low-first)/(last-first);b=(high-first)/(last-first);
    if(a>b)std::swap(a,b);
    a=std::max(0.f,a);b=std::min(1.f,b);return a<b;
}
}
