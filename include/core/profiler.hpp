// Thin wrapper around Tracy profiler.
// When TRACY_ENABLE is not defined, all macros expand to nothing (zero overhead).
#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
// No-op replacements when Tracy is disabled.
#define ZoneScoped
#define ZoneScopedN(x)
#define ZoneScopedC(x)
#define ZoneScopedNC(x, y)
#define FrameMark
#define FrameMarkNamed(x)
#define FrameMarkStart(x)
#define FrameMarkEnd(x)
#define TracyPlot(x, y)
#define TracyMessageL(x)
#endif
