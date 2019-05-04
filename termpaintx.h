#ifndef TERMPAINT_TERMPAINTX_INCLUDED
#define TERMPAINT_TERMPAINTX_INCLUDED

#include "termpaint.h"

#ifdef __cplusplus
extern "C" {
#endif

_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_available();
_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration(const char *options);
_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration_from_controlling_terminal(const char *options);
_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration_from_fd(int fd, _Bool auto_close, const char *options);

_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_wait_for_ready(termpaint_integration *integration);

_tERMPAINT_PUBLIC void termpaintx_full_integration_set_terminal(termpaint_integration *integration, termpaint_terminal *terminal);
_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_do_iteration(termpaint_integration *integration);

_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_terminal_size(termpaint_integration *integration, int *width, int *height);

_tERMPAINT_PUBLIC _Bool termpaintx_fd_set_termios(int fd, const char *options);
_tERMPAINT_PUBLIC _Bool termpaintx_fd_terminal_size(int fd, int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif
