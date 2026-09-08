#ifndef _GNM_DRIVER_H_
#define _GNM_DRIVER_H_

#include <stddef.h>
#include <stdint.h>

#include "gnm_types.h"
#include "gnm_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public API header for the sceGnm* runtime driver functions.
 *
 * Sourced from the shadPS4 reference declarations and re-expressed with
 * standard C types for the opengnm project.  Every function uses the
 * PS4_SYSV_ABI calling-convention attribute (defined in gnm_types.h).
 *
 * OrbisKernelEqueue / OrbisKernelEvent are replaced with void* because
 * opengnm does not ship Sony kernel headers.
 */
/* SceGnmDrawFlags (from the PS4 SDK ABI) */
typedef struct {
	uint32_t predication : 1;
	uint32_t _unused : 28;
	uint32_t rendertargetsliceoffset : 3;
} SceGnmDrawFlags;
_Static_assert(sizeof(SceGnmDrawFlags) == 0x4, "");
/* Draw commands */
int32_t PS4_SYSV_ABI sceGnmDrawIndex(uint32_t* cmdbuf, uint32_t size,
                                     uint32_t index_count, uintptr_t index_addr,
                                     uint32_t flags, uint32_t type);
int32_t PS4_SYSV_ABI sceGnmDrawIndexAuto(uint32_t* cmdbuf, uint32_t size,
                                         uint32_t index_count, uint32_t flags);
int32_t PS4_SYSV_ABI sceGnmDrawIndexIndirect(uint32_t* cmdbuf, uint32_t size,
                                             uint32_t data_offset,
                                             uint32_t shader_stage,
                                             uint32_t vertex_sgpr_offset,
                                             uint32_t instance_sgpr_offset,
                                             uint32_t flags);
int32_t PS4_SYSV_ABI sceGnmDrawIndexIndirectCountMulti(
    uint32_t* cmdbuf, uint32_t size, uint32_t data_offset, uint32_t max_count,
    uint64_t count_addr, uint32_t shader_stage, uint32_t vertex_sgpr_offset,
    uint32_t instance_sgpr_offset, uint32_t flags);
int PS4_SYSV_ABI sceGnmDrawIndexIndirectMulti(uint32_t* cmdbuf, uint32_t size,
                                              uint32_t data_offset,
                                              uint32_t max_count,
                                              uint32_t shader_stage,
                                              uint32_t vertex_sgpr_offset,
                                              uint32_t instance_sgpr_offset,
                                              uint32_t flags);
int PS4_SYSV_ABI sceGnmDrawIndexMultiInstanced(void);
int32_t PS4_SYSV_ABI sceGnmDrawIndexOffset(uint32_t* cmdbuf, uint32_t size,
                                           uint32_t index_offset,
                                           uint32_t index_count,
                                           uint32_t flags);
int32_t PS4_SYSV_ABI sceGnmDrawIndirect(uint32_t* cmdbuf, uint32_t size,
                                        uint32_t data_offset,
                                        uint32_t shader_stage,
                                        uint32_t vertex_sgpr_offset,
                                        uint32_t instance_sgpr_offset,
                                        uint32_t flags);
int PS4_SYSV_ABI sceGnmDrawIndirectCountMulti(void);
int32_t PS4_SYSV_ABI sceGnmDrawIndirectMulti(uint32_t* cmdbuf, uint32_t size,
                                             uint32_t data_offset,
                                             uint32_t max_count,
                                             uint32_t shader_stage,
                                             uint32_t vertex_sgpr_offset,
                                             uint32_t instance_sgpr_offset,
                                             uint32_t flags);
int PS4_SYSV_ABI sceGnmDrawOpaqueAuto(void);
/* Dispatch commands */
int32_t PS4_SYSV_ABI sceGnmDispatchDirect(uint32_t* cmdbuf, uint32_t size,
                                          uint32_t threads_x,
                                          uint32_t threads_y,
                                          uint32_t threads_z,
                                          uint32_t flags);
int32_t PS4_SYSV_ABI sceGnmDispatchIndirect(uint32_t* cmdbuf, uint32_t size,
                                            uint32_t data_offset,
                                            uint32_t flags);
int32_t PS4_SYSV_ABI sceGnmDispatchIndirectOnMec(uint32_t* cmdbuf,
                                                 uint32_t size,
                                                 uintptr_t args,
                                                 uint32_t modifier);
/* Shader set commands */
int32_t PS4_SYSV_ABI sceGnmSetCsShader(uint32_t* cmdbuf, uint32_t size,
                                       const uint32_t* cs_regs);
int32_t PS4_SYSV_ABI sceGnmSetCsShaderWithModifier(uint32_t* cmdbuf,
                                                   uint32_t size,
                                                   const uint32_t* cs_regs,
                                                   uint32_t modifier);
int32_t PS4_SYSV_ABI sceGnmSetEmbeddedPsShader(uint32_t* cmdbuf,
                                               uint32_t size,
                                               uint32_t shader_id,
                                               uint32_t shader_modifier);
int32_t PS4_SYSV_ABI sceGnmSetEmbeddedVsShader(uint32_t* cmdbuf,
                                               uint32_t size,
                                               uint32_t shader_id,
                                               uint32_t modifier);
int32_t PS4_SYSV_ABI sceGnmSetEsShader(uint32_t* cmdbuf, uint32_t size,
                                       const uint32_t* es_regs,
                                       uint32_t shader_modifier);
int PS4_SYSV_ABI sceGnmSetGsRingSizes(void);
int32_t PS4_SYSV_ABI sceGnmSetGsShader(uint32_t* cmdbuf, uint32_t size,
                                       const uint32_t* gs_regs);
int32_t PS4_SYSV_ABI sceGnmSetHsShader(uint32_t* cmdbuf, uint32_t size,
                                       const uint32_t* hs_regs,
                                       uint32_t param4);
int32_t PS4_SYSV_ABI sceGnmSetLsShader(uint32_t* cmdbuf, uint32_t size,
                                       const uint32_t* ls_regs,
                                       uint32_t shader_modifier);
int32_t PS4_SYSV_ABI sceGnmSetPsShader(uint32_t* cmdbuf, uint32_t size,
                                       const uint32_t* ps_regs);
int32_t PS4_SYSV_ABI sceGnmSetPsShader350(uint32_t* cmdbuf, uint32_t size,
                                          const uint32_t* ps_regs);
int32_t PS4_SYSV_ABI sceGnmSetVsShader(uint32_t* cmdbuf, uint32_t size,
                                       const uint32_t* vs_regs,
                                       uint32_t shader_modifier);
/* Shader update commands */
int32_t PS4_SYSV_ABI sceGnmUpdateGsShader(uint32_t* cmdbuf, uint32_t size,
                                          const uint32_t* gs_regs);
int PS4_SYSV_ABI sceGnmUpdateHsShader(uint32_t* cmdbuf, uint32_t size,
                                      const uint32_t* ps_regs,
                                      uint32_t ls_hs_config);
int32_t PS4_SYSV_ABI sceGnmUpdatePsShader(uint32_t* cmdbuf, uint32_t size,
                                          const uint32_t* ps_regs);
int32_t PS4_SYSV_ABI sceGnmUpdatePsShader350(uint32_t* cmdbuf, uint32_t size,
                                             const uint32_t* ps_regs);
int32_t PS4_SYSV_ABI sceGnmUpdateVsShader(uint32_t* cmdbuf, uint32_t size,
                                          const uint32_t* vs_regs,
                                          uint32_t shader_modifier);
/* Submit commands */
int PS4_SYSV_ABI sceGnmAreSubmitsAllowed(void);
int PS4_SYSV_ABI sceGnmRequestFlipAndSubmitDone(void);
int PS4_SYSV_ABI sceGnmRequestFlipAndSubmitDoneForWorkload(void);
int32_t PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffers(
    uint32_t count, void* const dcb_gpu_addrs[],
    uint32_t* dcb_sizes_in_bytes, void* const ccb_gpu_addrs[],
    uint32_t* ccb_sizes_in_bytes, uint32_t vo_handle, uint32_t buf_idx,
    uint32_t flip_mode, int64_t flip_arg);
int PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffersForWorkload(
    uint32_t workload, uint32_t count, void* const dcb_gpu_addrs[],
    uint32_t* dcb_sizes_in_bytes, void* const ccb_gpu_addrs[],
    uint32_t* ccb_sizes_in_bytes, uint32_t vo_handle, uint32_t buf_idx,
    uint32_t flip_mode, int64_t flip_arg);
int32_t PS4_SYSV_ABI sceGnmSubmitCommandBuffers(
    uint32_t count, void* const dcb_gpu_addrs[],
    uint32_t* dcb_sizes_in_bytes, void* const ccb_gpu_addrs[],
    uint32_t* ccb_sizes_in_bytes);
int PS4_SYSV_ABI sceGnmSubmitCommandBuffersForWorkload(
    uint32_t workload, uint32_t count, void* const dcb_gpu_addrs[],
    uint32_t* dcb_sizes_in_bytes, void* const ccb_gpu_addrs[],
    uint32_t* ccb_sizes_in_bytes);
int PS4_SYSV_ABI sceGnmSubmitDone(void);
/* Init / default hardware state */
uint32_t PS4_SYSV_ABI sceGnmDispatchInitDefaultHardwareState(uint32_t* cmdbuf,
                                                             uint32_t size);
uint32_t PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState(uint32_t* cmdbuf,
                                                         uint32_t size);
uint32_t PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState175(uint32_t* cmdbuf,
                                                            uint32_t size);
uint32_t PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState200(uint32_t* cmdbuf,
                                                            uint32_t size);
uint32_t PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState350(uint32_t* cmdbuf,
                                                            uint32_t size);
uint32_t PS4_SYSV_ABI sceGnmDrawInitToDefaultContextState(uint32_t* cmdbuf,
                                                          uint32_t size);
uint32_t PS4_SYSV_ABI sceGnmDrawInitToDefaultContextState400(uint32_t* cmdbuf,
                                                             uint32_t size);
int PS4_SYSV_ABI sceGnmDrawInitToDefaultContextStateInternalCommand(
    uint32_t* cmdbuf, uint32_t size);
int PS4_SYSV_ABI sceGnmDrawInitToDefaultContextStateInternalSize(void);
/* SDMA commands */
int PS4_SYSV_ABI sceGnmSdmaClose(void);
int PS4_SYSV_ABI sceGnmSdmaConstFill(void);
int PS4_SYSV_ABI sceGnmSdmaCopyLinear(void);
int PS4_SYSV_ABI sceGnmSdmaCopyTiled(void);
int PS4_SYSV_ABI sceGnmSdmaCopyWindow(void);
int PS4_SYSV_ABI sceGnmSdmaFlush(void);
int PS4_SYSV_ABI sceGnmSdmaGetMinCmdSize(void);
int PS4_SYSV_ABI sceGnmSdmaOpen(void);
/* Compute queue management */
int32_t PS4_SYSV_ABI sceGnmComputeWaitOnAddress(uint32_t* cmdbuf,
                                                uint32_t size,
                                                uintptr_t addr,
                                                uint32_t mask,
                                                uint32_t cmp_func,
                                                uint32_t ref);
int PS4_SYSV_ABI sceGnmComputeWaitSemaphore(void);
void PS4_SYSV_ABI sceGnmDingDong(uint32_t gnm_vqid, uint32_t next_offs_dw);
void PS4_SYSV_ABI sceGnmDingDongForWorkload(uint32_t gnm_vqid,
                                            uint32_t next_offs_dw,
                                            uint64_t workload_id);
int PS4_SYSV_ABI sceGnmMapComputeQueue(uint32_t pipe_id, uint32_t queue_id,
                                       uintptr_t ring_base_addr,
                                       uint32_t ring_size_dw,
                                       uint32_t* read_ptr_addr);
int PS4_SYSV_ABI sceGnmMapComputeQueueWithPriority(
    uint32_t pipe_id, uint32_t queue_id, uintptr_t ring_base_addr,
    uint32_t ring_size_dw, uint32_t* read_ptr_addr, uint32_t pipePriority);
int PS4_SYSV_ABI sceGnmUnmapComputeQueue(uint32_t vqid);
/* VGT / wave control */
int32_t PS4_SYSV_ABI sceGnmResetVgtControl(uint32_t* cmdbuf, uint32_t size);
int32_t PS4_SYSV_ABI sceGnmSetVgtControl(uint32_t* cmdbuf, uint32_t size,
                                         uint32_t prim_group_sz_minus_one,
                                         uint32_t partial_vs_wave_mode,
                                         uint32_t wd_switch_only_on_eop_mode);
int PS4_SYSV_ABI sceGnmSetWaveLimitMultiplier(void);
int PS4_SYSV_ABI sceGnmSetWaveLimitMultipliers(void);
int PS4_SYSV_ABI sceGnmSetSpiEnableSqCounters(void);
int PS4_SYSV_ABI sceGnmSetSpiEnableSqCountersForUnitInstance(void);
/* Validation */
int32_t PS4_SYSV_ABI sceGnmValidateCommandBuffers(void);
int PS4_SYSV_ABI sceGnmValidateDisableDiagnostics(void);
int PS4_SYSV_ABI sceGnmValidateDisableDiagnostics2(void);
int PS4_SYSV_ABI sceGnmValidateDispatchCommandBuffers(void);
int PS4_SYSV_ABI sceGnmValidateDrawCommandBuffers(void);
int PS4_SYSV_ABI sceGnmValidateGetDiagnosticInfo(void);
int PS4_SYSV_ABI sceGnmValidateGetDiagnostics(void);
int PS4_SYSV_ABI sceGnmValidateGetVersion(void);
bool PS4_SYSV_ABI sceGnmValidateOnSubmitEnabled(void);
int PS4_SYSV_ABI sceGnmValidateResetState(void);
int PS4_SYSV_ABI sceGnmValidationRegisterMemoryCheckCallback(void);
/* Resource registration */
int32_t PS4_SYSV_ABI sceGnmFindResourcesPublic(void);
int PS4_SYSV_ABI sceGnmFindResources(void);
int PS4_SYSV_ABI sceGnmGetResourceBaseAddressAndSizeInBytes(void);
int PS4_SYSV_ABI sceGnmGetResourceName(void);
int PS4_SYSV_ABI sceGnmGetResourceRegistrationBuffers(void);
int PS4_SYSV_ABI sceGnmGetResourceShaderGuid(void);
int PS4_SYSV_ABI sceGnmGetResourceType(void);
int PS4_SYSV_ABI sceGnmGetResourceUserData(void);
int PS4_SYSV_ABI sceGnmQueryResourceRegistrationUserMemoryRequirements(void);
int PS4_SYSV_ABI sceGnmRegisterGdsResource(void);
int32_t PS4_SYSV_ABI sceGnmRegisterOwner(void* handle, const char* name);
int PS4_SYSV_ABI sceGnmRegisterOwnerForSystem(void);
int32_t PS4_SYSV_ABI sceGnmRegisterResource(void* res_handle,
                                            void* owner_handle,
                                            const void* addr, size_t size,
                                            const char* name, int res_type,
                                            uint64_t user_data);
int PS4_SYSV_ABI sceGnmSetResourceRegistrationUserMemory(void);
int PS4_SYSV_ABI sceGnmSetResourceUserData(void);
int PS4_SYSV_ABI sceGnmUnregisterAllResourcesForOwner(void);
int PS4_SYSV_ABI sceGnmUnregisterOwnerAndResources(void);
int PS4_SYSV_ABI sceGnmUnregisterResource(void);
/* Workload management */
int PS4_SYSV_ABI sceGnmBeginWorkload(uint32_t workload_stream,
                                      uint64_t* workload);
int PS4_SYSV_ABI sceGnmCreateWorkloadStream(uint64_t param1,
                                            uint32_t* workload_stream);
int PS4_SYSV_ABI sceGnmDestroyWorkloadStream(void);
int PS4_SYSV_ABI sceGnmEndWorkload(uint64_t workload);
/* Event queue */
int32_t PS4_SYSV_ABI sceGnmAddEqEvent(void* eq, uint64_t id, void* udata);
int32_t PS4_SYSV_ABI sceGnmDeleteEqEvent(void* eq, uint64_t id);
int PS4_SYSV_ABI sceGnmGetEqEventType(const void* ev);
int PS4_SYSV_ABI sceGnmGetEqTimeStamp(void);
/* Thread trace (Sqtt) */
int PS4_SYSV_ABI sceGnmInsertThreadTraceMarker(void);
int PS4_SYSV_ABI sceGnmSqttFini(void);
int PS4_SYSV_ABI sceGnmSqttFinishTrace(void);
int PS4_SYSV_ABI sceGnmSqttGetBcInfo(void);
int PS4_SYSV_ABI sceGnmSqttGetGpuClocks(void);
int PS4_SYSV_ABI sceGnmSqttGetHiWater(void);
int PS4_SYSV_ABI sceGnmSqttGetStatus(void);
int PS4_SYSV_ABI sceGnmSqttGetTraceCounter(void);
int PS4_SYSV_ABI sceGnmSqttGetTraceWptr(void);
int PS4_SYSV_ABI sceGnmSqttGetWrapCounts(void);
int PS4_SYSV_ABI sceGnmSqttGetWrapCounts2(void);
int PS4_SYSV_ABI sceGnmSqttGetWritebackLabels(void);
int PS4_SYSV_ABI sceGnmSqttInit(void);
int PS4_SYSV_ABI sceGnmSqttSelectMode(void);
int PS4_SYSV_ABI sceGnmSqttSelectTarget(void);
int PS4_SYSV_ABI sceGnmSqttSelectTokens(void);
int PS4_SYSV_ABI sceGnmSqttSetCuPerfMask(void);
int PS4_SYSV_ABI sceGnmSqttSetDceEventWrite(void);
int PS4_SYSV_ABI sceGnmSqttSetHiWater(void);
int PS4_SYSV_ABI sceGnmSqttSetTraceBuffer2(void);
int PS4_SYSV_ABI sceGnmSqttSetTraceBuffers(void);
int PS4_SYSV_ABI sceGnmSqttSetUserData(void);
int PS4_SYSV_ABI sceGnmSqttSetUserdataTimer(void);
int PS4_SYSV_ABI sceGnmSqttStartTrace(void);
int PS4_SYSV_ABI sceGnmSqttStopTrace(void);
int PS4_SYSV_ABI sceGnmSqttSwitchTraceBuffer(void);
int PS4_SYSV_ABI sceGnmSqttSwitchTraceBuffer2(void);
int PS4_SYSV_ABI sceGnmSqttWaitForEvent(void);
/* Performance monitoring (Spm) */
int PS4_SYSV_ABI sceGnmSpmEndSpm(void);
int PS4_SYSV_ABI sceGnmSpmInit(void);
int PS4_SYSV_ABI sceGnmSpmInit2(void);
int PS4_SYSV_ABI sceGnmSpmSetDelay(void);
int PS4_SYSV_ABI sceGnmSpmSetMuxRam(void);
int PS4_SYSV_ABI sceGnmSpmSetMuxRam2(void);
int PS4_SYSV_ABI sceGnmSpmSetSelectCounter(void);
int PS4_SYSV_ABI sceGnmSpmSetSpmSelects(void);
int PS4_SYSV_ABI sceGnmSpmSetSpmSelects2(void);
int PS4_SYSV_ABI sceGnmSpmStartSpm(void);
/* Debugger */
int PS4_SYSV_ABI sceGnmDebugHardwareStatus(void);
int PS4_SYSV_ABI sceGnmDebugModuleReset(void);
int PS4_SYSV_ABI sceGnmDebugReset(void);
int PS4_SYSV_ABI sceGnmDebuggerGetAddressWatch(void);
int PS4_SYSV_ABI sceGnmDebuggerHaltWavefront(void);
int PS4_SYSV_ABI sceGnmDebuggerReadGds(void);
int PS4_SYSV_ABI sceGnmDebuggerReadSqIndirectRegister(void);
int PS4_SYSV_ABI sceGnmDebuggerResumeWavefront(void);
int PS4_SYSV_ABI sceGnmDebuggerResumeWavefrontCreation(void);
int PS4_SYSV_ABI sceGnmDebuggerSetAddressWatch(void);
int PS4_SYSV_ABI sceGnmDebuggerWriteGds(void);
int PS4_SYSV_ABI sceGnmDebuggerWriteSqIndirectRegister(void);
int PS4_SYSV_ABI sceGnmGetDbgGcHandle(void);

/* Razor GPU profiler/debugger exports (same module) */
int PS4_SYSV_ABI sceRazorCaptureCommandBuffersOnlyImmediate(void);
int PS4_SYSV_ABI sceRazorCaptureCommandBuffersOnlySinceLastFlip(void);
int PS4_SYSV_ABI sceRazorCaptureImmediate(void);
int PS4_SYSV_ABI sceRazorCaptureSinceLastFlip(void);
bool PS4_SYSV_ABI sceRazorIsLoaded(void);
/* Markers */
int32_t PS4_SYSV_ABI sceGnmInsertDingDongMarker(uint32_t* cmdbuf,
                                                uint32_t size);
int32_t PS4_SYSV_ABI sceGnmInsertPopMarker(uint32_t* cmdbuf, uint32_t size);
int32_t PS4_SYSV_ABI sceGnmInsertPushColorMarker(uint32_t* cmdbuf,
                                                 uint32_t size,
                                                 const char* marker,
                                                 uint32_t color);
int32_t PS4_SYSV_ABI sceGnmInsertPushMarker(uint32_t* cmdbuf, uint32_t size,
                                            const char* marker);
int PS4_SYSV_ABI sceGnmInsertSetColorMarker(void);
int32_t PS4_SYSV_ABI sceGnmInsertSetMarker(uint32_t* cmdbuf, uint32_t size,
                                           const char* marker);
int32_t PS4_SYSV_ABI sceGnmInsertWaitFlipDone(uint32_t* cmdbuf, uint32_t size,
                                              int32_t vo_handle,
                                              uint32_t buf_idx);
/* Coredump / misc */
void PS4_SYSV_ABI sceGnmFlushGarlic(void);
int PS4_SYSV_ABI sceGnmGetCoredumpAddress(void);
int PS4_SYSV_ABI sceGnmGetCoredumpMode(void);
int PS4_SYSV_ABI sceGnmGetCoredumpProtectionFaultTimestamp(void);
int PS4_SYSV_ABI sceGnmGetDebugTimestamp(void);
int PS4_SYSV_ABI sceGnmGetGpuBlockStatus(void);
uint32_t PS4_SYSV_ABI sceGnmGetGpuCoreClockFrequency(void);
int PS4_SYSV_ABI sceGnmGetGpuInfoStatus(void);
int PS4_SYSV_ABI sceGnmGetLastWaitedAddress(void);
int PS4_SYSV_ABI sceGnmGetNumTcaUnits(void);
int PS4_SYSV_ABI sceGnmGetOffChipTessellationBufferSize(void);
int PS4_SYSV_ABI sceGnmGetOwnerName(void);
int PS4_SYSV_ABI sceGnmGetPhysicalCounterFromVirtualized(void);
uint32_t PS4_SYSV_ABI sceGnmGetProtectionFaultTimeStamp(void);
int PS4_SYSV_ABI sceGnmGetShaderProgramBaseAddress(void);
int PS4_SYSV_ABI sceGnmGetShaderStatus(void);
uintptr_t
sceGnmGetTheTessellationFactorRingBufferBaseAddress(void) PS4_SYSV_ABI;
int PS4_SYSV_ABI sceGnmIsCoredumpValid(void);
int PS4_SYSV_ABI sceGnmRaiseUserExceptionEvent(void);
/* Driver internal */
bool PS4_SYSV_ABI sceGnmDriverCaptureInProgress(void);
uint32_t PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterface(void);
uint32_t PS4_SYSV_ABI
sceGnmDriverInternalRetrieveGnmInterfaceForGpuDebugger(void);
uint32_t PS4_SYSV_ABI
sceGnmDriverInternalRetrieveGnmInterfaceForGpuException(void);
uint32_t PS4_SYSV_ABI
sceGnmDriverInternalRetrieveGnmInterfaceForHDRScopes(void);
uint32_t PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterfaceForReplay(void);
uint32_t PS4_SYSV_ABI
sceGnmDriverInternalRetrieveGnmInterfaceForResourceRegistration(void);
uint32_t PS4_SYSV_ABI
sceGnmDriverInternalRetrieveGnmInterfaceForValidation(void);
int PS4_SYSV_ABI sceGnmDriverInternalVirtualQuery(void);
bool PS4_SYSV_ABI sceGnmDriverTraceInProgress(void);
int PS4_SYSV_ABI sceGnmDriverTriggerCapture(void);
void PS4_SYSV_ABI sceGnmRegisterGnmLiveCallbackConfig(void);
/* Logical CU / GPU PA */
void PS4_SYSV_ABI sceGnmGpuPaDebugEnter(void);
void PS4_SYSV_ABI sceGnmGpuPaDebugLeave(void);
bool PS4_SYSV_ABI sceGnmIsUserPaEnabled(void);
int PS4_SYSV_ABI sceGnmLogicalCuIndexToPhysicalCuIndex(void);
int32_t PS4_SYSV_ABI sceGnmLogicalCuMaskToPhysicalCuMask(int64_t,
                                                         int32_t logical_cu_mask);
int PS4_SYSV_ABI sceGnmLogicalTcaUnitToPhysical(void);
int PS4_SYSV_ABI sceGnmPaDisableFlipCallbacks(void);
int PS4_SYSV_ABI sceGnmPaEnableFlipCallbacks(void);
int PS4_SYSV_ABI sceGnmPaHeartbeat(void);
/* Mip stats */
int PS4_SYSV_ABI sceGnmDisableMipStatsReport(void);
int PS4_SYSV_ABI sceGnmRequestMipStatsReportAndReset(void);
int PS4_SYSV_ABI sceGnmSetupMipStatsReport(void);
/* Miscellaneous (unnamed NID-only exports from the same module) */
int PS4_SYSV_ABI Func_063D065A2D6359C3(void);
int PS4_SYSV_ABI Func_0CABACAFB258429D(void);
int PS4_SYSV_ABI Func_150CF336FC2E99A3(void);
int PS4_SYSV_ABI Func_17CA687F9EE52D49(void);
int PS4_SYSV_ABI Func_1870B89F759C6B45(void);
int PS4_SYSV_ABI Func_26F9029EF68A955E(void);
int PS4_SYSV_ABI Func_301E3DBBAB092DB0(void);
int PS4_SYSV_ABI Func_30BAFE172AF17FEF(void);
int PS4_SYSV_ABI Func_3E6A3E8203D95317(void);
int PS4_SYSV_ABI Func_40FEEF0C6534C434(void);
int PS4_SYSV_ABI Func_416B9079DE4CBACE(void);
int PS4_SYSV_ABI Func_4774D83BB4DDBF9A(void);
int PS4_SYSV_ABI Func_50678F1CCEEB9A00(void);
int PS4_SYSV_ABI Func_54A2EC5FA4C62413(void);
int PS4_SYSV_ABI Func_5A9C52C83138AE6B(void);
int PS4_SYSV_ABI Func_5D22193A31EA1142(void);
int PS4_SYSV_ABI Func_725A36DEBB60948D(void);
int PS4_SYSV_ABI Func_8021A502FA61B9BB(void);
int PS4_SYSV_ABI Func_9D002FE0FA40F0E6(void);
int PS4_SYSV_ABI Func_9D297F36A7028B71(void);
int PS4_SYSV_ABI Func_A2D7EC7A7BCF79B3(void);
int PS4_SYSV_ABI Func_AA12A3CB8990854A(void);
int PS4_SYSV_ABI Func_ADC8DDC005020BC6(void);
int PS4_SYSV_ABI Func_B0A8688B679CB42D(void);
int PS4_SYSV_ABI Func_B489020B5157A5FF(void);
int PS4_SYSV_ABI Func_BADE7B4C199140DD(void);
int PS4_SYSV_ABI Func_C4C328B7CF3B4171(void);
int PS4_SYSV_ABI Func_D1511B9DCFFB3DD9(void);
int PS4_SYSV_ABI Func_D53446649B02E58E(void);
int PS4_SYSV_ABI Func_D8B6E8E28E1EF0A3(void);
int PS4_SYSV_ABI Func_D93D733A19DD7454(void);
int PS4_SYSV_ABI Func_DE995443BC2A8317(void);
int PS4_SYSV_ABI Func_DF6E9528150C23FF(void);
int PS4_SYSV_ABI Func_ECB4C6BA41FE3350(void);
int PS4_SYSV_ABI Func_1C43886B16EE5530(void);
int PS4_SYSV_ABI Func_81037019ECCD0E01(void);
int PS4_SYSV_ABI Func_BFB41C057478F0BF(void);
int PS4_SYSV_ABI Func_E51D44DB8151238C(void);
int PS4_SYSV_ABI Func_F916890425496553(void);
/*
 * Internal PM4 packet builder wrappers (sceGnmDriver*).
 *
 * These are the packet-building functions that drawcommandbuffer.c calls.
 * On orbis, they delegate to the firmware's sceGnm* functions.
 * On generic, they emit PM4 packets directly.
 * These match the original driver.h API surface.
 */
int32_t sceGnmDriverDrawInitDefaultHardwareState350(
    uint32_t* cmd, uint32_t numdwords
);
int32_t sceGnmDriverDrawIndex(
    uint32_t* cmd, uint32_t numdwords, uint32_t indexcount,
    const void* indexaddr, SceGnmDrawFlags flags
);
int32_t sceGnmDriverDrawIndexAuto(
    uint32_t* cmd, uint32_t numdwords, uint32_t indexcount,
    SceGnmDrawFlags flags
);
int32_t sceGnmDriverDrawIndexIndirect(
    uint32_t* cmd, uint32_t numdwords, uint32_t dataoffset, uint32_t stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr, SceGnmDrawFlags flags
);
int32_t sceGnmDriverDrawIndirect(
    uint32_t* cmd, uint32_t numdwords, uint32_t dataoffset, uint32_t stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr, SceGnmDrawFlags flags
);
int32_t sceGnmDriverDrawIndexIndirectMulti(
    uint32_t* cmd, uint32_t numdwords, uint32_t dataoffset, uint32_t maxcount,
    uint32_t stage, uint8_t vertexoffusgpr, uint8_t instanceoffusgpr,
    SceGnmDrawFlags flags
);
int32_t sceGnmDriverDrawIndirectMulti(
    uint32_t* cmd, uint32_t numdwords, uint32_t dataoffset, uint32_t maxcount,
    uint32_t stage, uint8_t vertexoffusgpr, uint8_t instanceoffusgpr,
    SceGnmDrawFlags flags
);
int32_t sceGnmDriverDrawIndexIndirectCountMulti(
    uint32_t* cmd, uint32_t numdwords, uint32_t dataoffset, uint32_t maxcount,
    uint64_t countaddr, uint32_t stage, uint8_t vertexoffusgpr,
    uint8_t instanceoffusgpr, SceGnmDrawFlags flags
);
int32_t sceGnmDriverSetVsShader(
    uint32_t* cmd, uint32_t numdwords, const void* vsregs,
    uint32_t shadermodifier
);
int32_t sceGnmDriverSetPsShader(
    uint32_t* cmd, uint32_t numdwords, const void* psregs
);
int32_t sceGnmDriverSetPsShader350(
    uint32_t* cmd, uint32_t numdwords, const void* psregs
);
int32_t sceGnmDriverSetEmbeddedVsShader(
    uint32_t* cmd, uint32_t numdwords, int32_t shaderid, uint32_t shadermodifier
);
int32_t sceGnmDriverSetEmbeddedPsShader(
    uint32_t* cmd, uint32_t numdwords, int32_t shaderid
);
int32_t sceGnmDriverInsertWaitFlipDone(
    uint32_t* cmd, uint32_t numdwords, int32_t videohandle,
    uint32_t displaybufidx
);

#ifdef __cplusplus
}
#endif

#endif /* _GNM_DRIVER_H_ */
