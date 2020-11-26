// SPDX-License-Identifier: BSL-1.0
#ifndef TERMPAINT_TERMPAINT_IMAGE_INCLUDED
#define TERMPAINT_TERMPAINT_IMAGE_INCLUDED

#include <termpaint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && defined(TERMPAINT_EXPORT_SYMBOLS)
#define _tERMPAINT_PUBLIC __attribute__((visibility("default")))
#else
#define _tERMPAINT_PUBLIC
#endif

_tERMPAINT_PUBLIC _Bool termpaint_image_save(termpaint_surface *surface, const char* name);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_image_load(termpaint_terminal *term, const char *name);

#ifdef __cplusplus
}
#endif

#endif
