#pragma once

// Compatibility shim for building AMD FSR2 SDK sources on non-MSVC toolchains.
#include <stddef.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include <locale>
#include <codecvt>

#ifndef _countof
#define _countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef wcscpy_s
#define wcscpy_s(dst, src) wcscpy((dst), (src))
#endif
