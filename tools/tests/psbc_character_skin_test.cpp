#include "psbc_compile.h"
extern "C" {
#include "gnm_helpers.h"
#include "pm4/amdgfxregs.h"
}
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
int main(int argc,char**argv){
 assert(argc==3); const bool shadow=std::strcmp(argv[2],"shadow")==0;
 FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 std::vector<uint32_t>s(n/4);assert(fread(s.data(),1,n,f)==size_t(n));fclose(f);
 PsbcDescriptorBinding ubo[]={{0,6,1,0,16,UINT32_MAX}};
 PsbcDescriptorBinding bone[]={{0,7,1,0,16,UINT32_MAX}};
 // Unused material set retains production set numbering; no shader resource references it.
 PsbcDescriptorSetLayout sets[]={{1,16,0,ubo},{0,0,0,nullptr},{1,16,0,bone}};
 if(shadow){sets[0]={0,0,0,nullptr};sets[1]={1,16,0,bone};}
 PsbcCompileOptions o{};o.target=PSBC_TARGET_PS4_BASE;o.stage=PSBC_STAGE_VERTEX;o.entrypoint="main";o.optimise=true;
 o.descriptor_set_count=shadow?2:3;o.descriptor_sets=sets;o.descriptor_address32_hi=2;
 PsbcShaderOutput out{};psbc_init();auto r=psbc_compile_shader(s.data(),n,&o,&out);
 printf("%s %s result=%u\n",argv[1],argv[2],r);assert(r==PSBC_RESULT_OK);
 GnmShaderMetadata m{};assert(sceGnmShaderBinaryGetMetadata(out.data,out.size,&m)==0);assert(m.type==GNM_SHADER_VERTEX);
 uint32_t mask=0,regs=0;bool fetch=false,vertices=false,push=false;
 for(uint32_t i=0;i<m.numinputusageslots;i++){
 auto&a=m.inputusageslots[i];bool wide=a.usagetype==GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER||a.usagetype==GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE;
 unsigned width=wide?2:1;assert(a.startregister+width<=16);unsigned bits=((1u<<width)-1)<<a.startregister;assert(!(regs&bits));regs|=bits;
 if(a.usagetype==GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE)mask|=1u<<a.apislot;
 fetch|=a.usagetype==GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER;vertices|=a.usagetype==GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE;
 push|=(a.usagetype==GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE&&a.apislot==254)||a.usagetype==GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST;
 printf("usage type=%u api=%u reg=%u\n",a.usagetype,a.apislot,a.startregister);
 }
 assert(mask==(shadow?2u:5u));assert(fetch&&vertices&&push);assert(m.numinputsemantics==(shadow?4u:6u));
 auto*v=static_cast<const GnmVsShader*>(m.stage);uint32_t reg=v->registers.spishaderpgmrsrc1vs;
 printf("PASS codeBytes=%u binaryBytes=%zu descriptorMask=%u inputSemantics=%u exportSemantics=%u allocatedVGPR=%u allocatedSGPR=%u rsrc1=%08x\n",m.shadercodesize,out.size,mask,m.numinputsemantics,m.numexportsemantics,(G_00B128_VGPRS(reg)+1)*4,(G_00B128_SGPRS(reg)+1)*8,reg);
 psbc_free_output(&out);psbc_shutdown();
}
