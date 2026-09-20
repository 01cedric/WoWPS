#include "pipeline/blp_loader.hpp"
#include "rendering/m2_blend_mode.hpp"
#include <cassert>
#include <cstdio>
using namespace wowee::pipeline;
int main(){
 unsigned cases=0;
 for(unsigned a0:{0u,1u,127u,254u,255u})for(unsigned a1:{0u,1u,127u,254u,255u})for(unsigned index=0;index<8;++index){
  BLPImage image;image.width=image.height=4;image.compression=BLPCompression::DXT5;image.mipmaps={{}};
  auto&b=image.mipmaps[0];b.resize(16);b[0]=a0;b[1]=a1;unsigned long long indices=0;
  for(unsigned i=0;i<16;i++)indices|=(unsigned long long)index<<(3*i);
  for(unsigned i=0;i<6;i++)b[2+i]=(indices>>(8*i))&255;
  auto decoded=BLPLoader::decodeBaseLevel(image);assert(decoded.size()==64);bool transparency=false;
  for(unsigned i=3;i<decoded.size();i+=4)transparency|=decoded[i]!=255;
  assert(image.hasTransparency()==transparency);
  if(a0==255&&a1==255){assert(transparency==(index==6));assert(decoded[3]==(index==6?0:255));}
  cases++;
 }
 // Metadata does not change authored opaque or alpha-key classification.
 using namespace wowee::rendering;
 assert(!m2BatchNeedsAlphaTest(M2_BLEND_OPAQUE,true));assert(!m2BatchNeedsAlphaTest(M2_BLEND_OPAQUE,false));
 assert(m2BatchNeedsAlphaTest(M2_BLEND_ALPHA_KEY,true));assert(m2BatchNeedsAlphaTest(M2_BLEND_ALPHA_KEY,false));
 assert(!m2BatchNeedsAlphaTest(M2_BLEND_ALPHA,true));assert(m2BatchNeedsAlphaTest(M2_BLEND_ALPHA,false));
 printf("PASS %u DXT5 endpoint/selector cases: compressed transparency matches actual RGBA decoder; equal255 endpoints selector6 transparent, selector7 opaque; opaque/alpha-key blend semantics unchanged\n",cases);
}
