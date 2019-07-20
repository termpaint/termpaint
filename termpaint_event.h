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

_tERMPAINT_PUBLIC const char *termpaint_input_i_resync(void);

_tERMPAINT_PUBLIC const char *termpaint_input_enter(void);
_tERMPAINT_PUBLIC const char *termpaint_input_space(void);
_tERMPAINT_PUBLIC const char *termpaint_input_tab(void);
_tERMPAINT_PUBLIC const char *termpaint_input_backspace(void);
_tERMPAINT_PUBLIC const char *termpaint_input_context_menu(void);
_tERMPAINT_PUBLIC const char *termpaint_input_delete(void);
_tERMPAINT_PUBLIC const char *termpaint_input_end(void);
_tERMPAINT_PUBLIC const char *termpaint_input_home(void);
_tERMPAINT_PUBLIC const char *termpaint_input_insert(void);
_tERMPAINT_PUBLIC const char *termpaint_input_page_down(void);
_tERMPAINT_PUBLIC const char *termpaint_input_page_up(void);
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_down(void);
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_left(void);
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_right(void);
_tERMPAINT_PUBLIC const char *termpaint_input_arrow_up(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_divide(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_multiply(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_subtract(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_add(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_enter(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad_decimal(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad0(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad1(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad2(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad3(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad4(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad5(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad6(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad7(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad8(void);
_tERMPAINT_PUBLIC const char *termpaint_input_numpad9(void);
_tERMPAINT_PUBLIC const char *termpaint_input_escape(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f1(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f2(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f3(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f4(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f5(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f6(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f7(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f8(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f9(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f10(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f11(void);
_tERMPAINT_PUBLIC const char *termpaint_input_f12(void);

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
