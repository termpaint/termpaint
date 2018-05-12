#ifndef TERMPAINT_TERMPAINT_EVENT_INCLUDED
#define TERMPAINT_TERMPAINT_EVENT_INCLUDED

#ifdef __cplusplus
#ifndef _Bool
#define _Bool bool
#endif
#endif

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

#define TERMPAINT_EV_UNKNOWN 0
#define TERMPAINT_EV_CHAR 1
#define TERMPAINT_EV_KEY 2
#define TERMPAINT_EV_OVERFLOW 3
#define TERMPAINT_EV_INVALID_UTF8 4
#define TERMPAINT_EV_CURSOR_POSITION 5
#define TERMPAINT_EV_RAW_SEC_DEV_ATTRIB 6
#define TERMPAINT_EV_AUTO_DETECT_FINISHED 7

#define TERMPAINT_MOD_SHIFT 1
#define TERMPAINT_MOD_CTRL 2
#define TERMPAINT_MOD_ALT 4
#define TERMPAINT_MOD_ALTGR 8

struct termpaint_event_ {
    int type;
    unsigned length;
    const char *atom_or_string;
    int modifier;
    // valid for EV_CURSOR_POSITION
    int x;
    int y;
};
typedef struct termpaint_event_ termpaint_event;

#ifdef __cplusplus
}
#endif

#endif
