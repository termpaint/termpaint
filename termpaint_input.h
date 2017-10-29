#ifndef TERMPAINT_TERMPAINT_INPUT_INCLUDED
#define TERMPAINT_TERMPAINT_INPUT_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

const char *termpaint_input_i_resync();

const char *termpaint_input_enter();
const char *termpaint_input_space();
const char *termpaint_input_tab();
const char *termpaint_input_backspace();
const char *termpaint_input_context_menu();
const char *termpaint_input_delete();
const char *termpaint_input_end();
const char *termpaint_input_home();
const char *termpaint_input_insert();
const char *termpaint_input_page_down();
const char *termpaint_input_page_up();
const char *termpaint_input_arrow_down();
const char *termpaint_input_arrow_left();
const char *termpaint_input_arrow_right();
const char *termpaint_input_arrow_up();
const char *termpaint_input_numpad_divide();
const char *termpaint_input_numpad_multiply();
const char *termpaint_input_numpad_subtract();
const char *termpaint_input_numpad_add();
const char *termpaint_input_numpad_enter();
const char *termpaint_input_numpad_decimal();
const char *termpaint_input_numpad0();
const char *termpaint_input_numpad1();
const char *termpaint_input_numpad2();
const char *termpaint_input_numpad3();
const char *termpaint_input_numpad4();
const char *termpaint_input_numpad5();
const char *termpaint_input_numpad6();
const char *termpaint_input_numpad7();
const char *termpaint_input_numpad8();
const char *termpaint_input_numpad9();
const char *termpaint_input_escape();
const char *termpaint_input_f1();
const char *termpaint_input_f2();
const char *termpaint_input_f3();
const char *termpaint_input_f4();
const char *termpaint_input_f5();
const char *termpaint_input_f6();
const char *termpaint_input_f7();
const char *termpaint_input_f8();
const char *termpaint_input_f9();
const char *termpaint_input_f10();
const char *termpaint_input_f11();
const char *termpaint_input_f12();

#define TERMPAINT_EV_CHAR 1
#define TERMPAINT_EV_KEY 2
#define TERMPAINT_EV_OVERFLOW 3
#define TERMPAINT_EV_INVALID_UTF8 4

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
void termpaint_input_set_raw_filter_cb(termpaint_input *ctx, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data);
void termpaint_input_set_event_cb(termpaint_input *ctx, void (*cb)(void *user_data, termpaint_input_event* event), void *user_data);
_Bool termpaint_input_add_data(termpaint_input *ctx, const char *data, unsigned length);
void termpaint_input_timeout(termpaint_input *ctx);

const char* termpaint_input_peek_buffer(termpaint_input *ctx);
int termpaint_input_peek_buffer_length(termpaint_input *ctx);


#ifdef __cplusplus
}
#endif

#endif
