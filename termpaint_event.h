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

#if defined(__GNUC__) && defined(TERMPAINT_EXPORT_SYMBOLS)
#define _tERMPAINT_PUBLIC __attribute__((visibility("default")))
#else
#define _tERMPAINT_PUBLIC
#endif

_tERMPAINT_PUBLIC const char *termpaint_input_i_resync();

_tERMPAINT_PUBLIC const char *termpaint_input_enter();
_tERMPAINT_PUBLIC const char *termpaint_input_space();
_tERMPAINT_PUBLIC const char *termpaint_input_tab();
_tERMPAINT_PUBLIC const char *termpaint_input_backspace();
_tERMPAINT_PUBLIC const char *termpaint_input_context_menu();
_tERMPAINT_PUBLIC const char *termpaint_input_delete();
_tERMPAINT_PUBLIC const char *termpaint_input_end();
_tERMPAINT_PUBLIC const char *termpaint_input_home();
_tERMPAINT_PUBLIC const char *termpaint_input_insert();
_tERMPAINT_PUBLIC const char *termpaint_input_page_down();
_tERMPAINT_PUBLIC const char *termpaint_input_page_up();
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_down();
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_left();
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_right();
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_up();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_divide();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_multiply();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_subtract();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_add();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_enter();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_decimal();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad0();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad1();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad2();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad3();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad4();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad5();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad6();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad7();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad8();
_tERMPAINT_PUBLIC const char *termpaint_input_numpad9();
_tERMPAINT_PUBLIC const char *termpaint_input_escape();
_tERMPAINT_PUBLIC const char *termpaint_input_f1();
_tERMPAINT_PUBLIC const char *termpaint_input_f2();
_tERMPAINT_PUBLIC const char *termpaint_input_f3();
_tERMPAINT_PUBLIC const char *termpaint_input_f4();
_tERMPAINT_PUBLIC const char *termpaint_input_f5();
_tERMPAINT_PUBLIC const char *termpaint_input_f6();
_tERMPAINT_PUBLIC const char *termpaint_input_f7();
_tERMPAINT_PUBLIC const char *termpaint_input_f8();
_tERMPAINT_PUBLIC const char *termpaint_input_f9();
_tERMPAINT_PUBLIC const char *termpaint_input_f10();
_tERMPAINT_PUBLIC const char *termpaint_input_f11();
_tERMPAINT_PUBLIC const char *termpaint_input_f12();

#define TERMPAINT_EV_UNKNOWN 0
#define TERMPAINT_EV_CHAR 1
#define TERMPAINT_EV_KEY 2
#define TERMPAINT_EV_AUTO_DETECT_FINISHED 3
#define TERMPAINT_EV_OVERFLOW 4
#define TERMPAINT_EV_INVALID_UTF8 5
#define TERMPAINT_EV_CURSOR_POSITION 6
#define TERMPAINT_EV_MODE_REPORT 7
#define TERMPAINT_EV_COLOR_SLOT_REPORT 8
#define TERMPAINT_EV_REPAINT_REQUESTED 9
#define TERMPAINT_EV_MOUSE 10

#define TERMPAINT_EV_RAW_PRI_DEV_ATTRIB 100
#define TERMPAINT_EV_RAW_SEC_DEV_ATTRIB 101
#define TERMPAINT_EV_RAW_3RD_DEV_ATTRIB 102
#define TERMPAINT_EV_RAW_DECREQTPARM 103

#define TERMPAINT_MOD_SHIFT 1
#define TERMPAINT_MOD_CTRL 2
#define TERMPAINT_MOD_ALT 4
#define TERMPAINT_MOD_ALTGR 8

#define TERMPAINT_MOUSE_PRESS 1
#define TERMPAINT_MOUSE_RELEASE 2
#define TERMPAINT_MOUSE_MOVE 3

struct termpaint_event_ {
    int type;
    union {
        // EV_CHAR and INVALID_UTF8
        struct {
            unsigned length;
            const char *string;
            int modifier;
        } c;

        // EV_KEY
        struct {
            unsigned length;
            const char *atom;
            int modifier;
        } key;

        // EV_MOUSE
        struct {
            int x;
            int y;
            int raw_btn_and_flags;
            int action; // TERMPAINT_MOUSE_*
            int button; // button == 3 means release with unknown button
            int modifier;
        } mouse;

        // EV_CURSOR_POSITION
        struct {
            int x;
            int y;
            _Bool safe;
        } cursor_position;

        // EV_MODE_REPORT
        struct {
            int number;
            int kind;
            int status;
        } mode;

        // TERMPAINT_EV_RAW_SEC_DEV_ATTRIB, etc
        struct {
            unsigned length;
            const char *string;
        } raw;

        struct {
            int slot;
            const char *color;
            unsigned length;
        } color_slot_report;
    };
};
typedef struct termpaint_event_ termpaint_event;

#ifdef __cplusplus
}
#endif

#endif
