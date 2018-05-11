#ifndef TERMPAINT_TERMPAINTX_INCLUDED
#define TERMPAINT_TERMPAINTX_INCLUDED

#include "termpaint.h"

#ifdef __cplusplus
extern "C" {
#endif

_Bool termpaint_full_integration_available();
termpaint_integration *termpaint_full_integration();
termpaint_integration *termpaint_full_integration_from_controlling_terminal();
termpaint_integration *termpaint_full_integration_from_fd(int fd, _Bool auto_close);

_Bool termpaint_full_integration_wait_for_ready(termpaint_integration *integration);

void termpaint_full_integration_set_terminal(termpaint_integration *integration, termpaint_terminal *terminal);
_Bool termpaint_full_integration_do_iteration(termpaint_integration *integration);

_Bool termpaint_full_integration_terminal_size(termpaint_integration *integration, int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif
