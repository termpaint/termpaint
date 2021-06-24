// SPDX-License-Identifier: BSL-1.0
#ifndef TERMPAINT_TERMPAINT_IMAGE_INCLUDED
#define TERMPAINT_TERMPAINT_IMAGE_INCLUDED

#include <stdio.h>

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
_tERMPAINT_PUBLIC _Bool termpaint_image_save_to_file(termpaint_surface *surface, FILE *file);
_tERMPAINT_PUBLIC char* termpaint_image_save_alloc_buffer(termpaint_surface *surface);
_tERMPAINT_PUBLIC void termpaint_image_save_dealloc_buffer(char *buffer);

_tERMPAINT_PUBLIC termpaint_surface *termpaint_image_load(termpaint_terminal *term, const char *name);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_image_load_from_file(termpaint_terminal *term, FILE *file);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_image_load_from_buffer(termpaint_terminal *term, char *buffer, int length);

#ifdef __cplusplus
}
#endif

#endif
