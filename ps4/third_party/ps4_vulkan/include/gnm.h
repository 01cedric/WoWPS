#ifndef _GNM_H_
#define _GNM_H_

/* opengnm — Sony PS4 GNM SDK-compatible API
 *
 * This master include pulls in the full public API surface.
 * Any PS4 app or game written against the official Sony SDK headers
 * should compile and link against opengnm unmodified.
 *
 * Include this single header for the complete API:
 *   #include <gnm.h>
 *
 * Or include individual headers for specific subsystems.
 */

/* Core types and calling convention */
#include "gnm_types.h"

/* Error codes and message handling */
#include "gnm_error.h"

/* Control register bitfield structs */
#include "gnm_controls.h"

/* Data format utilities and predefined formats */
#include "gnm_dataformat.h"

/* Resource types */
#include "gnm_buffer.h"
#include "gnm_sampler.h"
#include "gnm_texture.h"
#include "gnm_rendertarget.h"
#include "gnm_depthrendertarget.h"

/* Shader types: stage registers, fetch shader, input/export semantics */
#include "gnm_shader.h"

/* Shader binary container format */
#include "gnm_shaderbinary.h"

/* Command buffer */
#include "gnm_commandbuffer.h"

/* Draw command buffer — PM4 command buffer building */
#include "gnm_drawcommandbuffer.h"

/* Runtime driver — sceGnm* runtime functions (draw/submit/shader-set/etc) */
#include "gnmdriver.h"

/* Surface computation — sceGpa* */
#include "gpuaddr.h"

/* Platform detection (GpuMode, buffer label address) */
#include "platform.h"

/* Convenience helpers for common app/renderer setup paths */
#include "gnm_helpers.h"

#endif /* _GNM_H_ */
