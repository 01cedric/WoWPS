#ifndef _GNM_DRAWCOMMANDBUFFER_H_
#define _GNM_DRAWCOMMANDBUFFER_H_

#include <stdint.h>

#include "gnm_buffer.h"
#include "gnm_commandbuffer.h"
#include "gnm_controls.h"
#include "gnm_depthrendertarget.h"
#include "gnm_sampler.h"
#include "gnm_shader.h"
#include "gnm_texture.h"

OPENGNM_EXTERN_C_BEGIN

typedef struct {
	float dmin;
	float dmax;
	float scale[3];
	float offset[3];
} GnmSetViewportInfo;

/* === Init === */
void sceGnmDrawCmdInitDefaultHardwareState(GnmCommandBuffer* cmd);

/* === Draw commands === */
void sceGnmDrawCmdDrawIndex(
    GnmCommandBuffer* cmd, uint32_t indexcount, const void* indexaddr
);
void sceGnmDrawCmdDrawIndex2(
    GnmCommandBuffer* cmd, uint32_t indexcount, const void* indexaddr,
    GnmDrawModifier modifier
);
void sceGnmDrawCmdDrawIndexAuto(GnmCommandBuffer* cmd, uint32_t indexcount);
void sceGnmDrawCmdDrawIndexAuto2(
    GnmCommandBuffer* cmd, uint32_t indexcount, GnmDrawModifier modifier
);
void sceGnmDrawCmdDrawIndexIndirect(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
void sceGnmDrawCmdDrawIndexIndirect2(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr, GnmDrawModifier mod
);
void sceGnmDrawCmdDrawIndirect(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
void sceGnmDrawCmdDrawIndirect2(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr, GnmDrawModifier mod
);
void sceGnmDrawCmdDrawIndexIndirectMulti(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t maxcount,
    GnmShaderStage stage, uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
void sceGnmDrawCmdDrawIndirectMulti(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t maxcount,
    GnmShaderStage stage, uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
void sceGnmDrawCmdDrawIndexIndirectCountMulti(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t maxcount,
    uint64_t countaddr, GnmShaderStage stage, uint8_t vertexoffusgpr,
    uint8_t instanceoffusgpr
);
void sceGnmDrawCmdDrawIndexOffset(
    GnmCommandBuffer* cmd, uint32_t indexoffset, uint32_t indexcount,
    GnmDrawModifier modifier
);

/* === State setup === */
void sceGnmDrawCmdSetDepthClearValue(GnmCommandBuffer* cmd, float clearvalue);
void sceGnmDrawCmdSetDepthRenderTarget(
    GnmCommandBuffer* cmd, const GnmDepthRenderTarget* depthtarget
);
void sceGnmDrawCmdSetGuardBands(
    GnmCommandBuffer* cmd, float horzclip, float vertclip, float horzdiscard,
    float vertdiscard
);
void sceGnmDrawCmdSetHwScreenOffset(
    GnmCommandBuffer* cmd, uint32_t offsetx, uint32_t offsety
);
void sceGnmDrawCmdSetIndexBuffer(GnmCommandBuffer* cmd, const void* buffer);
void sceGnmDrawCmdSetIndexCount(GnmCommandBuffer* cmd, uint32_t count);
void sceGnmDrawCmdSetIndexSize(
    GnmCommandBuffer* cmd, GnmIndexSize indexsize, GnmCachePolicy cachepol
);
void sceGnmDrawCmdSetIndirectArgs(
    GnmCommandBuffer* cmd, const GnmDrawIndirectArgs* args
);
void sceGnmDrawCmdSetIndexedIndirectArgs(
    GnmCommandBuffer* cmd, const GnmDrawIndexedIndirectArgs* args
);
void sceGnmDrawCmdSetInstanceStepRate(
    GnmCommandBuffer* cmd, uint32_t rate0, uint32_t rate1
);
void sceGnmDrawCmdSetNumInstances(GnmCommandBuffer* cmd, uint32_t count);
void sceGnmDrawCmdSetPrimitiveType(
    GnmCommandBuffer* cmd, GnmPrimitiveType primType
);
void sceGnmDrawCmdSetRenderTarget(
    GnmCommandBuffer* cmd, uint32_t rtslot, const GnmRenderTarget* rt
);
void sceGnmDrawCmdSetRenderTargetMask(GnmCommandBuffer* cmd, uint32_t mask);
void sceGnmDrawCmdSetScreenScissor(
    GnmCommandBuffer* cmd, int32_t left, int32_t top, int32_t right,
    int32_t bottom
);
void sceGnmDrawCmdSetViewport(
    GnmCommandBuffer* cmd, uint32_t viewportid, const GnmSetViewportInfo* vpinfo
);

/* === Shader set === */
void sceGnmDrawCmdSetPsShader(
    GnmCommandBuffer* cmd, const GnmPsStageRegisters* regs
);
void sceGnmDrawCmdSetEmbeddedPsShader(
    GnmCommandBuffer* cmd, GnmEmbeddedPsShader shaderid
);
void sceGnmDrawCmdSetVsShader(
    GnmCommandBuffer* cmd, const GnmVsStageRegisters* regs,
    uint32_t shadermodifier
);
void sceGnmDrawCmdSetEmbeddedVsShader(
    GnmCommandBuffer* cmd, GnmEmbeddedVsShader shaderid, uint32_t shadermodifier
);
void sceGnmDrawCmdSetCsShader(
    GnmCommandBuffer* cmd, const GnmCsStageRegisters* regs
);
void sceGnmDrawCmdSetCsShaderWithModifier(
    GnmCommandBuffer* cmd, const GnmCsStageRegisters* regs,
    uint32_t shadermodifier
);
void sceGnmDrawCmdSetGsShader(
    GnmCommandBuffer* cmd, const GnmGsStageRegisters* regs
);
void sceGnmDrawCmdSetEsShader(
    GnmCommandBuffer* cmd, const GnmEsStageRegisters* regs,
    uint32_t shadermodifier
);
void sceGnmDrawCmdSetHsShader(
    GnmCommandBuffer* cmd, const GnmHsStageRegisters* regs,
    uint32_t lshsconfig
);
void sceGnmDrawCmdSetLsShader(
    GnmCommandBuffer* cmd, const GnmLsStageRegisters* regs,
    uint32_t shadermodifier
);

/* === Dispatch === */
void sceGnmDrawCmdDispatchDirect(
    GnmCommandBuffer* cmd, uint32_t threadsx, uint32_t threadsy,
    uint32_t threadsz, uint32_t flags
);
void sceGnmDrawCmdDispatchIndirect(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t flags
);

/* === PS input usage === */
void sceGnmDrawCmdSetPsInputUsage(
    GnmCommandBuffer* cmd, const GnmVertexExportSemantic* vstable,
    uint32_t numvstableitems, const GnmPixelInputSemantic* pstable,
    uint32_t numpstableitems
);

/* === Resource binding (user data) === */
void sceGnmDrawCmdSetVsharpUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    const GnmBuffer* buf
);
void sceGnmDrawCmdSetTsharpUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    const GnmTexture* tex
);
void sceGnmDrawCmdSetSsharpUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    const GnmSampler* sampler
);
void sceGnmDrawCmdSetPointerUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    void* ptr
);

/* === Control registers === */
void sceGnmDrawCmdSetBlendControl(
    GnmCommandBuffer* cmd, uint32_t rtindex, const GnmBlendControl* ctrl
);
void sceGnmDrawCmdSetBlendColor(
    GnmCommandBuffer* cmd, float red, float green, float blue, float alpha
);
void sceGnmDrawCmdSetDepthStencilControl(
    GnmCommandBuffer* cmd, const GnmDepthStencilControl* ctrl
);
void sceGnmDrawCmdSetDbRenderControl(
    GnmCommandBuffer* cmd, const GnmDbRenderControl* ctrl
);
void sceGnmDrawCmdSetPrimitiveSetup(
    GnmCommandBuffer* cmd, const GnmPrimitiveSetup* ctrl
);
void sceGnmDrawCmdSetViewportTransformControl(
    GnmCommandBuffer* cmd, const GnmViewportTransformControl* ctrl
);

/* === Sync / events === */
void sceGnmDrawCmdEventWriteEop(
    GnmCommandBuffer* cmd, GnmEventType evtype, uint64_t gpuaddr,
    GnmEventDataSel datasel, uint64_t immvalue
);
bool sceGnmDrawCmdFillMemory(
    GnmCommandBuffer* cmd, uint64_t gpuaddr, uint32_t sizebytes,
    uint32_t value
);
bool sceGnmDrawCmdCopyMemory(
    GnmCommandBuffer* cmd, uint64_t dstaddr, uint64_t srcaddr,
    uint32_t sizebytes
);
void sceGnmDrawCmdWaitGraphicsWrite(
    GnmCommandBuffer* cmd, GnmAcquireTargetFlags targets
);
void sceGnmDrawCmdWaitMem(
    GnmCommandBuffer* cmd, GnmWaitRegMemFunc op, uint64_t gpuaddr,
    uint32_t refval, uint32_t mask
);
void sceGnmDrawCmdWaitUntilSafeForRendering(
    GnmCommandBuffer* cmd, int32_t videohandle, uint32_t displaybufidx
);

/* === Transform feedback (stream-out) === */
void sceGnmDrawCmdSetStreamOutConfig(
    GnmCommandBuffer* cmd, uint32_t streamen, uint32_t raststream,
    uint32_t bufferen
);
void sceGnmDrawCmdSetStreamOutBuffer(
    GnmCommandBuffer* cmd, uint32_t slot, uint64_t gpuaddr, uint32_t size,
    uint32_t stride
);

/* === Occlusion queries === */
void sceGnmDrawCmdResetQuery(
    GnmCommandBuffer* cmd, uint64_t gpuaddr
);
void sceGnmDrawCmdBeginQuery(
    GnmCommandBuffer* cmd, uint64_t gpuaddr
);
void sceGnmDrawCmdEndQuery(
    GnmCommandBuffer* cmd, uint64_t gpuaddr
);

OPENGNM_EXTERN_C_END

#endif /* _GNM_DRAWCOMMANDBUFFER_H_ */
