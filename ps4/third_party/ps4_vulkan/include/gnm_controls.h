#ifndef _GNM_CONTROLS_H_
#define _GNM_CONTROLS_H_

#include <stdbool.h>
#include <stdint.h>

#include "gnm_types.h"

typedef struct {
	bool blendenabled;
	GnmCombFunc colorfunc;
	GnmBlendOp colorsrcmult;
	GnmBlendOp colordstmult;
	GnmCombFunc alphafunc;
	GnmBlendOp alphasrcmult;
	GnmBlendOp alphadstmult;
	bool separatealphaenable;
} GnmBlendControl;

typedef struct {
	bool depthclearenable;
	bool stencilclearenable;
	bool htileresummarizeenable;
	bool depthwritebackpol;
	bool stencilwritebackpol;
	bool forcedepthdecompress;
	bool copycentroidenable;
	uint8_t copysampleindex;
	bool copydepthtocolorenable;
	bool copystenciltocolorenable;
} GnmDbRenderControl;

typedef struct {
	bool zwrite;
	GnmDepthCompare zfunc;
	GnmDepthCompare stencilfunc;
	GnmDepthCompare stencilbackfunc;
	bool separatestencilenable;
	bool depthenable;
	bool stencilenable;
	bool depthboundsenable;
} GnmDepthStencilControl;

typedef struct {
	GnmCullMode cullmode;
	GnmFaceOrientation frontface;
	GnmFillMode frontmode;
	GnmFillMode backmode;
	bool frontoffsetmode;
	bool backoffsetmode;
	bool vertexwindowoffsetenable;
	GnmProvokingVertex provokemode;
	bool perspectivecorrectiondisable;
} GnmPrimitiveSetup;

typedef struct {
	bool scalex;
	bool offsetx;
	bool scaley;
	bool offsety;
	bool scalez;
	bool offsetz;
	bool perspectivedividexy;
	bool perspectivedividez;
	bool invertw;
} GnmViewportTransformControl;

#endif /* _GNM_CONTROLS_H_ */
