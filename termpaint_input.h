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
termpaint_input *termpaint_input_new();
void termpaint_input_free(termpaint_input *ctx);
void termpaint_input_set_raw_filter_cb(termpaint_input *ctx, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data);
void termpaint_input_set_event_cb(termpaint_input *ctx, void (*cb)(void *user_data, termpaint_event* event), void *user_data);
_Bool termpaint_input_add_data(termpaint_input *ctx, const char *data, unsigned length);

const char* termpaint_input_peek_buffer(termpaint_input *ctx);
int termpaint_input_peek_buffer_length(termpaint_input *ctx);


#ifdef __cplusplus
}
#endif

#endif
