#ifndef _GNM_SAMPLER_H_
#define _GNM_SAMPLER_H_

#include <stdbool.h>
#include <stdint.h>

#include "gnm_error.h"
#include "gnm_types.h"

typedef struct {
	/* register 0 */
	GnmTexClamp clampx : 3;
	GnmTexClamp clampy : 3;
	GnmTexClamp clampz : 3;
	uint32_t maxanisoratio : 3;
	GnmDepthCompare depthcomparefunc : 3;
	uint32_t forceunormalized : 1;
	uint32_t anisothreshold : 3;
	uint32_t mccoordtrunc : 1;
	uint32_t forcedegamma : 1;
	uint32_t anisobias : 6;
	uint32_t trunccoord : 1;
	uint32_t disablecubewrap : 1;
	GnmFilterMode filtermode : 2;
	uint32_t _unused : 1;

	/* register 1 */
	uint32_t minlod : 12;
	uint32_t maxlod : 12;
	uint32_t perfmip : 4;
	uint32_t perfz : 4;

	/* register 2 */
	uint32_t lodbias : 14;
	uint32_t lodbiassec : 6;
	GnmFilter xymagfilter : 2;
	GnmFilter xyminfilter : 2;
	GnmZFilter zfilter : 2;
	GnmMipFilter mipfilter : 2;
	uint32_t mippointpreclamp : 1;
	uint32_t disablelsbceil : 1;
	uint32_t _unused2 : 2;

	/* register 3 */
	uint32_t bordercolorptr : 12;
	uint32_t _unused3 : 18;
	GnmBorderColor bordercolortype : 2;
} GnmSampler;
_Static_assert(sizeof(GnmSampler) == 0x10, "");

static inline uint8_t sceGnmSampGetAnisotropyRatio(const GnmSampler* s) {
	return 1 << s->maxanisoratio;
}

#endif /* _GNM_SAMPLER_H_ */
