// SPDX-License-Identifier: BSL-1.0
#ifndef TERMPAINT_TERMPAINTX_INCLUDED
#define TERMPAINT_TERMPAINTX_INCLUDED

#include "termpaint.h"

#ifdef __cplusplus
extern "C" {
#endif

_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_available(void);
_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration(const char *options);
_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration_from_controlling_terminal(const char *options);
_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration_from_fd(int fd, _Bool auto_close, const char *options);
_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration_from_fds(int fd_read, int fd_write, const char *options);

_tERMPAINT_PUBLIC termpaint_integration *termpaintx_full_integration_setup_terminal_fullscreen(const char *options, void (*event_handler)(void *, termpaint_event *), void *event_handler_user_data, termpaint_terminal **terminal_out);

_tERMPAINT_PUBLIC void termpaintx_full_integration_wait_for_ready(termpaint_integration *integration);
_tERMPAINT_PUBLIC void termpaintx_full_integration_wait_for_ready_with_message(termpaint_integration *integration, int milliseconds, const char* message);
_tERMPAINT_PUBLIC void termpaintx_full_integration_apply_input_quirks(termpaint_integration *integration);

_tERMPAINT_PUBLIC void termpaintx_full_integration_set_terminal(termpaint_integration *integration, termpaint_terminal *terminal);
_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_do_iteration(termpaint_integration *integration);
_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_do_iteration_with_timeout(termpaint_integration *integration, int *milliseconds);

_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_terminal_size(termpaint_integration *integration, int *width, int *height);

_tERMPAINT_PUBLIC _Bool termpaintx_full_integration_ttyrescue_start(termpaint_integration *integration);

_tERMPAINT_PUBLIC const struct termios *termpaintx_full_integration_original_terminal_attributes(termpaint_integration *integration);

_tERMPAINT_PUBLIC _Bool termpaintx_fd_set_termios(int fd, const char *options);
_tERMPAINT_PUBLIC _Bool termpaintx_fd_terminal_size(int fd, int *width, int *height);

typedef void (*termpaint_logging_func)(struct termpaint_integration_ *integration, char *data, int length);

_tERMPAINT_PUBLIC termpaint_logging_func termpaintx_enable_tk_logging(void);

#ifdef __cplusplus
}
#endif

#endif
