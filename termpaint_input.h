#ifndef TERMPAINT_TERMPAINT_INPUT_INCLUDED
#define TERMPAINT_TERMPAINT_INPUT_INCLUDED

#include <termpaint_event.h>

#ifdef __cplusplus
#ifndef _Bool
#define _Bool bool
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif


struct termpaint_input_;
typedef struct termpaint_input_ termpaint_input;

// Usually termpaint_terminal's input integration should be used instead of raw termpaint_input
_tERMPAINT_PUBLIC termpaint_input *termpaint_input_new(void);
_tERMPAINT_PUBLIC void termpaint_input_free(termpaint_input *ctx);
_tERMPAINT_PUBLIC void termpaint_input_set_raw_filter_cb(termpaint_input *ctx, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data);
_tERMPAINT_PUBLIC void termpaint_input_set_event_cb(termpaint_input *ctx, void (*cb)(void *user_data, termpaint_event* event), void *user_data);
_tERMPAINT_PUBLIC _Bool termpaint_input_add_data(termpaint_input *ctx, const char *data, unsigned length);

_tERMPAINT_PUBLIC void termpaint_input_expect_cursor_position_report(termpaint_input *ctx);

#define TERMPAINT_INPUT_EXPECT_NO_LEGACY_MOUSE 0
#define TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE 1
#define TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE_MODE_1005 2
_tERMPAINT_PUBLIC void termpaint_input_expect_legacy_mouse_reports(termpaint_input *ctx, int s);
_tERMPAINT_PUBLIC void termpaint_input_handle_paste(termpaint_input *ctx, _Bool enable);
_tERMPAINT_PUBLIC void termpaint_input_expect_apc_sequences(termpaint_input *ctx, _Bool enable);

_tERMPAINT_PUBLIC const char* termpaint_input_peek_buffer(const termpaint_input *ctx);
_tERMPAINT_PUBLIC int termpaint_input_peek_buffer_length(const termpaint_input *ctx);


#define TERMPAINT_INPUT_QUIRK_BACKSPACE_X08_AND_X7F_SWAPPED          1

_tERMPAINT_PUBLIC void termpaint_input_activate_quirk(termpaint_input *ctx, int quirk);


#ifdef __cplusplus
}
#endif

#endif
