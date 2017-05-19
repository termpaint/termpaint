#ifndef TERMPAINT_TERMPAINT_INPUT_INCLUDED
#define TERMPAINT_TERMPAINT_INPUT_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

const char *termpaint_input_enter();
const char *termpaint_input_space();
const char *termpaint_input_tab();
// TODO add rest

#define TERMPAINT_EV_CHAR 1
#define TERMPAINT_EV_KEY 2

#define TERMPAINT_MOD_SHIFT 1
#define TERMPAINT_MOD_CTRL 2
#define TERMPAINT_MOD_ALT 4

struct termpaint_input_event_ {
    int type;
    unsigned length;
    const char *atom_or_string;
    int modifier;
};
typedef struct termpaint_input_event_ termpaint_input_event;

struct termpaint_input_;
typedef struct termpaint_input_ termpaint_input;

termpaint_input *termpaint_input_new();
void termpaint_input_set_raw_filter_cb(termpaint_input *ctx, _Bool (*cb)(void *user_data, const char *data, unsigned length), void *user_data);
void termpaint_input_set_event_cb(termpaint_input *ctx, void (*cb)(void *user_data, termpaint_input_event* event), void *user_data);
_Bool termpaint_input_add_data(termpaint_input *ctx, const char *data, unsigned length);
void termpaint_input_timeout(termpaint_input *ctx);

const char* termpaint_input_peek_buffer(termpaint_input *ctx);
int termpaint_input_peek_buffer_length(termpaint_input *ctx);


#ifdef __cplusplus
}
#endif

#endif
