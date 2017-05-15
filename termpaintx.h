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

_Bool termpaint_full_integration_poll_ready(termpaint_integration *integration);

#ifdef __cplusplus
}
#endif

#endif
