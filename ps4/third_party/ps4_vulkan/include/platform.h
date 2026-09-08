#ifndef _GNM_PLATFORM_H_
#define _GNM_PLATFORM_H_

#include <stdint.h>

#include "gnm_types.h"

OPENGNM_EXTERN_C_BEGIN

GnmGpuMode PS4_SYSV_ABI sceGnmGpuMode(void);

typedef struct {
	GnmGpuMode gpumode;
	int32_t (*getbufferlabeladdress)(
	    int32_t videohandle, uint64_t* outaddr
	);
} GnmPlatParams;

void sceGnmPlatInit(GnmPlatParams* params);
int32_t PS4_SYSV_ABI sceGnmPlatGetBufferLabelAddress(int32_t videohandle, uint64_t* outaddr);

OPENGNM_EXTERN_C_END

#endif /* _GNM_PLATFORM_H_ */
