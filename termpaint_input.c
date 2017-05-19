#include "termpaint_input.h"

#include <string.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h> // for exit

#include "termpaint_compiler.h"

#include "termpaint_utf8.h"

/* Known problems:
 *  * alt-ESC is seen as ESC ESC
 *  * in modOther mode: ctrl-backspace misidentfies as ctrl-H (ctrl-H has other code)
 *  * similar for tab, esc, return
 *  * in modOther ctrl-? strange (utf 8 converter?)
 */


#define nullptr ((void*)0)

#define DEF_ATOM(name, value) \
static const char ATOM_ ## name[] = value; \
const char *termpaint_input_ ## name () { return ATOM_ ## name; }


DEF_ATOM(i_resync, "i_resync")

// Naming based on W3C uievents-code spec
DEF_ATOM(enter, "Enter")
DEF_ATOM(space, "Space")
DEF_ATOM(tab, "Tab")
DEF_ATOM(backspace, "Backspace")
DEF_ATOM(context_menu, "ContextMenu")

DEF_ATOM(delete, "Delete")
DEF_ATOM(end, "End")
// help ommited
DEF_ATOM(home, "Home")
DEF_ATOM(insert, "Insert")
DEF_ATOM(page_down, "PageDown")
DEF_ATOM(page_up, "PageUp")

DEF_ATOM(arrow_down, "ArrowDown")
DEF_ATOM(arrow_left, "ArrowLeft")
DEF_ATOM(arrow_right, "ArrowRight")
DEF_ATOM(arrow_up, "ArrowUp")

DEF_ATOM(numpad_divide, "NumpadDivide")
DEF_ATOM(numpad_multiply, "NumpadMultiply")
DEF_ATOM(numpad_subtract, "NumpadSubtract")
DEF_ATOM(numpad_add, "NumpadAdd")
DEF_ATOM(numpad_enter, "NumpadEnter")
DEF_ATOM(numpad_decimal, "NumpadDecimal")
DEF_ATOM(numpad0, "Numpad0")
DEF_ATOM(numpad1, "Numpad1")
DEF_ATOM(numpad2, "Numpad2")
DEF_ATOM(numpad3, "Numpad3")
DEF_ATOM(numpad4, "Numpad4")
DEF_ATOM(numpad5, "Numpad5")
DEF_ATOM(numpad6, "Numpad6")
DEF_ATOM(numpad7, "Numpad7")
DEF_ATOM(numpad8, "Numpad8")
DEF_ATOM(numpad9, "Numpad9")

DEF_ATOM(escape, "Escape")

DEF_ATOM(f1, "F1")
DEF_ATOM(f2, "F2")
DEF_ATOM(f3, "F3")
DEF_ATOM(f4, "F4")
DEF_ATOM(f5, "F5")
DEF_ATOM(f6, "F6")
DEF_ATOM(f7, "F7")
DEF_ATOM(f8, "F8")
DEF_ATOM(f9, "F9")
DEF_ATOM(f10, "F10")
DEF_ATOM(f11, "F11")
DEF_ATOM(f12, "F12")



#define MOD_CTRL TERMPAINT_MOD_CTRL
#define MOD_ALT TERMPAINT_MOD_ALT
#define MOD_SHIFT TERMPAINT_MOD_SHIFT

#define MOD_PRINT (1 << 31)
#define MOD_ENTER (1 << 31 + 1 << 30)
//#define MOD_PRINT (1 << 31)

struct key_mapping_entry_ {
    const char *sequence;
    const char *atom;
    int modifiers;
};
typedef struct key_mapping_entry_ key_mapping_entry;

#define XTERM_MODS(PREFIX, POSTFIX, ATOM)                               \
{ PREFIX "2" POSTFIX, ATOM,                      MOD_SHIFT },       \
    { PREFIX "3" POSTFIX, ATOM,            MOD_ALT             },   \
    { PREFIX "4" POSTFIX, ATOM,            MOD_ALT | MOD_SHIFT },   \
    { PREFIX "5" POSTFIX, ATOM, MOD_CTRL                       },   \
    { PREFIX "6" POSTFIX, ATOM, MOD_CTRL           | MOD_SHIFT },   \
    { PREFIX "7" POSTFIX, ATOM, MOD_CTRL | MOD_ALT             },   \
    { PREFIX "8" POSTFIX, ATOM, MOD_CTRL | MOD_ALT | MOD_SHIFT }


// keyboard settings to consider:
// xterm:
//    xterm.vt100.translations: <KeyPress>: insert() --> remove all xterm side keybindings
//    xterm.vt100.modifyCursorKeys \in (-1, 0, 1, 2, 3)
//    xterm.vt100.modifyFunctionKeys \in (-1, 0, 1, 2, 3)
//    xterm.vt100.modifyKeyboard ??
//    xterm.vt100.modifyOtherKeys ??
//    xterm.vt100.oldXtermFKeys ??
// Modes:
//    ?1
//    ?66           keypad mapping changes
//    ?67
//    ?1035
//    ?1036
//    ?1039
//    ?1050  ???
//    ?1051  ???
//    ?1052  ???
//    ?1053  ???
//    ?1060  ???
//    ?1061  ???

static key_mapping_entry key_mapping_table[] = {
    //{ "\x0a", ATOM_enter, 0 }, also ctrl_something
    // no modifiers for enter in xterm normal mode
    XTERM_MODS("\e[27;", ";13~", ATOM_enter), // modifiy other keys mode
    XTERM_MODS("\e[13;", "u", ATOM_enter), // modifiy other keys mode

    //{ "\x09", ATOM_tab, 0 }, also ctrl_something
    { "\e[Z", ATOM_tab, MOD_SHIFT }, // xterm, normal mode
    XTERM_MODS("\e[27;", ";9~", ATOM_tab), // modifiy other keys mode
    XTERM_MODS("\e[9;", "u", ATOM_tab), // modifiy other keys mode

    // TODO check other modifiers without WM
    { " ", ATOM_space, 0 },
    // { "\x00", ATOM_SPACE, MOD_CTRL } via special case in code
    XTERM_MODS("\e[27;", ";32~", ATOM_space), // modifiy other keys mode
    XTERM_MODS("\e[32;", "u", ATOM_space), // modifiy other keys mode
    //+ also ctrl-2

    { "\e[29~", ATOM_context_menu, 0 },
    XTERM_MODS("\e[29;", "~", ATOM_context_menu),

    { "\e[3~", ATOM_delete, 0 },
    XTERM_MODS("\e[3;", "~", ATOM_delete),
    { "\e[F", ATOM_end, 0},
    XTERM_MODS("\e[1;", "F", ATOM_end),
    { "\e[H", ATOM_home, 0},
    XTERM_MODS("\e[1;", "H", ATOM_home),
    { "\e[2~", ATOM_insert, 0},
    XTERM_MODS("\e[2;", "~", ATOM_insert),
    { "\e[6~", ATOM_page_down, 0},
    XTERM_MODS("\e[6;", "~", ATOM_page_down),
    { "\e[5~", ATOM_page_up, 0},
    XTERM_MODS("\e[5;", "~", ATOM_page_up), // shift combinations only available when scroll bindings are removed in xterm

    { "\e[B", ATOM_arrow_down, 0 },
    XTERM_MODS("\e[1;", "B", ATOM_arrow_down),
    { "\e[D", ATOM_arrow_left, 0 },
    XTERM_MODS("\e[1;", "D", ATOM_arrow_left),
    { "\e[C", ATOM_arrow_right, 0 },
    XTERM_MODS("\e[1;", "C", ATOM_arrow_right),
    { "\e[A", ATOM_arrow_up, 0 },
    XTERM_MODS("\e[1;", "A", ATOM_arrow_up),

    // non application mode
    { "\e[E", ATOM_numpad5, 0 },
    XTERM_MODS("\e[1;", "E", ATOM_arrow_up), // shift combinations not reachable in xterm

    // application mode (?66)
    { "\eOo", ATOM_numpad_divide, 0 },
    XTERM_MODS("\eO", "o", ATOM_numpad_divide), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOj", ATOM_numpad_multiply, 0 },
    XTERM_MODS("\eO", "j", ATOM_numpad_multiply), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOm", ATOM_numpad_subtract, 0 },
    XTERM_MODS("\eO", "m", ATOM_numpad_subtract), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOk", ATOM_numpad_add, 0 },
    XTERM_MODS("\eO", "k", ATOM_numpad_add), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOM", ATOM_numpad_enter, 0 },
    XTERM_MODS("\eO", "M", ATOM_numpad_enter),
    //{ "\e[3~", ATOM_numpad_decimal, 0 },
    //XTERM_MODS("\e[3;", "~", ATOM_numpad_decimal), // shifted combinations produce other codes in xterm
    { "\eO2l", ATOM_numpad_decimal,                      MOD_SHIFT },
    { "\eO6l", ATOM_numpad_decimal, MOD_CTRL           | MOD_SHIFT },
    { "\eO4l", ATOM_numpad_decimal,            MOD_ALT | MOD_SHIFT },
    { "\eO8l", ATOM_numpad_decimal, MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2p", ATOM_numpad0,                             MOD_SHIFT },
    { "\eO6p", ATOM_numpad0,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4p", ATOM_numpad0,                   MOD_ALT | MOD_SHIFT },
    { "\eO8p", ATOM_numpad0,        MOD_CTRL | MOD_ALT | MOD_SHIFT },


    { "\eO2q", ATOM_numpad1,                             MOD_SHIFT },
    { "\eO6q", ATOM_numpad1,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4q", ATOM_numpad1,                   MOD_ALT | MOD_SHIFT },
    { "\eO8q", ATOM_numpad1,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2r", ATOM_numpad2,                             MOD_SHIFT },
    { "\eO6r", ATOM_numpad2,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4r", ATOM_numpad2,                   MOD_ALT | MOD_SHIFT },
    { "\eO8r", ATOM_numpad2,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2s", ATOM_numpad3,                             MOD_SHIFT },
    { "\eO6s", ATOM_numpad3,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4s", ATOM_numpad3,                   MOD_ALT | MOD_SHIFT },
    { "\eO8s", ATOM_numpad3,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2t", ATOM_numpad4,                             MOD_SHIFT },
    { "\eO6t", ATOM_numpad4,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4t", ATOM_numpad4,                   MOD_ALT | MOD_SHIFT },
    { "\eO8t", ATOM_numpad4,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2u", ATOM_numpad5,                             MOD_SHIFT },
    { "\eO6u", ATOM_numpad5,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4u", ATOM_numpad5,                   MOD_ALT | MOD_SHIFT },
    { "\eO8u", ATOM_numpad5,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2v", ATOM_numpad6,                             MOD_SHIFT },
    { "\eO6v", ATOM_numpad6,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4v", ATOM_numpad6,                   MOD_ALT | MOD_SHIFT },
    { "\eO8v", ATOM_numpad6,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2w", ATOM_numpad7,                             MOD_SHIFT },
    { "\eO6w", ATOM_numpad7,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4w", ATOM_numpad7,                   MOD_ALT | MOD_SHIFT },
    { "\eO8w", ATOM_numpad7,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2x", ATOM_numpad8,                             MOD_SHIFT },
    { "\eO6x", ATOM_numpad8,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4x", ATOM_numpad8,                   MOD_ALT | MOD_SHIFT },
    { "\eO8x", ATOM_numpad8,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    { "\eO2y", ATOM_numpad9,                             MOD_SHIFT },
    { "\eO6y", ATOM_numpad9,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4y", ATOM_numpad9,                   MOD_ALT | MOD_SHIFT },
    { "\eO8y", ATOM_numpad9,        MOD_CTRL | MOD_ALT | MOD_SHIFT },

    // ESC ---> same as Ctrl-[
    XTERM_MODS("\e[27;", ";27~", ATOM_escape), // modifiy other keys mode
    XTERM_MODS("\e[27;", "u", ATOM_escape), // modifiy other keys mode


    { "\eOP", ATOM_f1, 0 },
    XTERM_MODS("\e[1;", "P", ATOM_f1),
    XTERM_MODS("\eO", "P", ATOM_f1),
    { "\eOQ", ATOM_f2, 0 },
    XTERM_MODS("\e[1;", "Q", ATOM_f2),
    XTERM_MODS("\eO", "Q", ATOM_f2),
    { "\eOR", ATOM_f3, 0 },
    XTERM_MODS("\e[1;", "R", ATOM_f3),
    XTERM_MODS("\eO", "R", ATOM_f3),
    { "\eOS", ATOM_f4, 0 },
    XTERM_MODS("\e[1;", "S", ATOM_f4),    
    XTERM_MODS("\eO", "S", ATOM_f4),

    { "\e[15~", ATOM_f5, 0 },
    XTERM_MODS("\e[15;", "~", ATOM_f5),
    { "\e[17~", ATOM_f6, 0 },
    XTERM_MODS("\e[17;", "~", ATOM_f6),
    { "\e[18~", ATOM_f7, 0 },
    XTERM_MODS("\e[18;", "~", ATOM_f7),
    { "\e[19~", ATOM_f8, 0 },
    XTERM_MODS("\e[19;", "~", ATOM_f8),
    { "\e[20~", ATOM_f9, 0 },
    XTERM_MODS("\e[20;", "~", ATOM_f9),
    { "\e[21~", ATOM_f10, 0 },
    XTERM_MODS("\e[21;", "~", ATOM_f10),
    { "\e[23~", ATOM_f11, 0 },
    XTERM_MODS("\e[23;", "~", ATOM_f11),
    { "\e[24~", ATOM_f12, 0 },
    XTERM_MODS("\e[24;", "~", ATOM_f12),


    //{ "", ATOM_, 0 },
    //{ "", ATOM_, 0 },
    //{ "", ATOM_, 0 },

    { "\x01", "a", MOD_PRINT},
    { "\x02", "b", MOD_PRINT },
    { "\x03", "c", MOD_PRINT },
    { "\x04", "d", MOD_PRINT },
    { "\x05", "e", MOD_PRINT },
    { "\x06", "f", MOD_PRINT },
    { "\x07", "g", MOD_PRINT },
    { "\x08", "h", MOD_PRINT },
    //+ also ctrl-Backspace
    { "\x09", "i", MOD_PRINT },
    //+ also Tab, Ctrl-Tab
    { "\x0a", "j", MOD_PRINT },
    //+ also Return, Ctrl-Return
    { "\x0b", "k", MOD_PRINT },
    { "\x0c", "l", MOD_PRINT },
    { "\x0d", "m", MOD_PRINT },
    { "\x0e", "n", MOD_PRINT },
    { "\x0f", "o", MOD_PRINT },
    { "\x10", "p", MOD_PRINT },
    { "\x11", "q", MOD_PRINT },
    { "\x12", "r", MOD_PRINT },
    { "\x13", "s", MOD_PRINT },
    { "\x14", "t", MOD_PRINT },
    { "\x15", "u", MOD_PRINT },
    { "\x16", "v", MOD_PRINT },
    { "\x17", "w", MOD_PRINT },
    { "\x18", "x", MOD_PRINT },
    { "\x19", "y", MOD_PRINT },
    { "\x1a", "z", MOD_PRINT },
    { "\x1b", "[", MOD_PRINT },
    //+ also ESC
    //+ also ctrl-3
    { "\x1c", "\\", MOD_PRINT },
    //+ also ctrl-4
    { "\x1d", "]", MOD_PRINT },
    //+ also ctrl-5
    { "\x1e", "~", MOD_PRINT },
    //+ also ctrl-6
    { "\x1f", "?", MOD_PRINT },
    //+ also ctrl-7
    { "\x7f", ATOM_backspace, 0 },
    XTERM_MODS("\e[27;", ";127~", ATOM_backspace), // modifiy other keys mode
    XTERM_MODS("\e[127;", "u", ATOM_backspace), // modifiy other keys mode

    { "\e[0n", ATOM_i_resync, 0 },
    { 0, 0, 0 }
};


void termpaintp_input_selfcheck() {
    bool ok = true;
    for (key_mapping_entry* entry_a = key_mapping_table; entry_a->sequence != nullptr; entry_a++) {
        for (key_mapping_entry* entry_b = entry_a; entry_b->sequence != nullptr; entry_b++) {
            if (entry_a != entry_b && strcmp(entry_a->sequence, entry_b->sequence) == 0) {
                printf("Duplicate key mapping: %s == %s\n", entry_a->atom, entry_b->atom);
                ok = false;
            }
        }
    }
    if (!ok) {
        exit(55);
    }
}


#define MAX_SEQ_LENGTH 1024

enum termpaint_input_state {
    tpis_base,
    tpis_esc,
    tpis_ss3,
    tpis_csi,
    tpis_cmd_str,
    tpis_str_terminator_esc,
    tpid_utf8_5, tpid_utf8_4, tpid_utf8_3, tpid_utf8_2, tpid_utf8_1
};

struct termpaint_input_ {
    char buff[MAX_SEQ_LENGTH];
    int used;
    enum termpaint_input_state state;

    _Bool (*raw_filter_cb)(void *user_data, const char *data, unsigned length);
    void *raw_filter_user_data;

    void (*event_cb)(void *, termpaint_input_event *);
    void *event_user_data;
};



static void termpaintp_input_reset(termpaint_input *ctx) {
    ctx->buff[0] = 0;
    ctx->used = 0;
    ctx->state = tpis_base;
}

static void termpaintp_input_raw(termpaint_input *ctx, const char *data, size_t length) {
    if (ctx->raw_filter_cb) {
        if (ctx->raw_filter_cb(ctx->raw_filter_user_data, data, length)) {
            return;
        }
    }
    if (!ctx->event_cb) {
        return;
    }

    char buffer[6];

    termpaint_input_event event;
    event.type = 0;
    if (length == 1 && data[0] == 0) {
        event.type = TERMPAINT_EV_KEY;
        event.length = 0;
        event.atom_or_string = ATOM_space;
        event.modifier = MOD_CTRL;
    } else {
        // TODO optimize
        for (key_mapping_entry* entry = key_mapping_table; entry->sequence != nullptr; entry++) {
            if (strlen(entry->sequence) == length && memcmp(entry->sequence, data, length) == 0) {
                if (entry->modifiers & MOD_PRINT) {
                    // special case for ctrl-X which is in the table but a modified printable
                    event.type = TERMPAINT_EV_CHAR;
                    event.length = length;
                    event.atom_or_string = entry->atom;
                    event.modifier = MOD_CTRL;
                } else {
                    event.type = TERMPAINT_EV_KEY;
                    event.length = 0;
                    event.atom_or_string = entry->atom;
                    event.modifier = entry->modifiers;
                }
            }
        }
        // the nice xterm extensions:
        // \e[27;<mod>;<char>~
        // \e[<char>;<mod>u  (resource only selectable variant)
        if (!event.type && (
                    (length >= 9 && memcmp(data, "\e[27;", 5) == 0 && data[length-1] == '~')
                 || (length >= 6 && data[0] == '\e' && data[1] == '[' && data[length-1] == 'u'))) {
            // TODO \e[<mod>;<char>u
            unsigned i;
            if (data[length-1] == 'u') {
                i = 2;
            } else {
                // ~ variant
                i = 5;
            }
            int p = 0;
            int state = 0;
            int first = -1;
            int mod, codepoint;
            for (; i < length-1; i++) {
                if (data[i] >= '0' && data[i] <= '9') {
                    p = p * 10 + data[i]-'0';
                } else if (state == 0 && data[i] == ';') {
                    first = p;
                    p = 0;
                    state = 1;
                } else {
                    state = -1;
                    break;
                }
            }
            if (data[length-1] == 'u') {
                mod = p;
                codepoint = first;
            } else {
                // ~ variant
                mod = first;
                codepoint = p;
            }

            if (state == 1 && codepoint > 0 && codepoint <= 0x7FFFFFFF) {
                // TODO exclude C0 space, C1 space and 0x7f
                event.type = TERMPAINT_EV_CHAR;
                event.length = termpaintp_encode_to_utf8(codepoint, buffer);
                event.atom_or_string = buffer;
                event.modifier = 0;
                mod = mod - 1;
                if (mod & 1) {
                    event.modifier |= MOD_SHIFT;
                }
                if (mod & 2) {
                    event.modifier |= MOD_ALT;
                }
                if (mod & 4) {
                    event.modifier |= MOD_CTRL;
                }
            }
        }
        if (!event.type && length > 2 && data[0] == '\e' && (0xc0 == (0xc0 & data[1]))) {
            // tokenizer should ensure that this is exactly one valid utf8 codepoint
            event.type = TERMPAINT_EV_CHAR;
            event.length = length-1;
            event.atom_or_string = data+1;
            event.modifier = MOD_ALT;
        }
        if (!event.type && length == 2 && data[0] == '\e' && data[1] > 32 && data[1] < 127) {
            event.type = TERMPAINT_EV_CHAR;
            event.length = length-1;
            event.atom_or_string = data+1;
            event.modifier = MOD_ALT;
        }
        if (!event.type && length > 1 && (0xc0 == (0xc0 & data[0]))) {
            // tokenizer should ensure that this is exactly one valid utf8 codepoint
            event.type = TERMPAINT_EV_CHAR;
            event.length = length;
            event.atom_or_string = data;
            event.modifier = 0;
        }
        if (!event.type && length > 0 && data[0] > 32 && data[0] < 127) {
            event.type = TERMPAINT_EV_CHAR;
            event.length = length;
            event.atom_or_string = data;
            event.modifier = 0;
        }
    }
    ctx->event_cb(ctx->event_user_data, &event);
}

termpaint_input *termpaint_input_new() {
    termpaintp_input_selfcheck();
    termpaint_input *ctx = calloc(1, sizeof(termpaint_input));
    termpaintp_input_reset(ctx);
    ctx->raw_filter_cb = nullptr;
    ctx->event_cb = nullptr;
    return ctx;
}

void termpaint_input_set_raw_filter_cb(termpaint_input *ctx, _Bool (*cb)(void *user_data, const char *data, unsigned length), void *user_data) {
    ctx->raw_filter_cb = cb;
    ctx->raw_filter_user_data = user_data;
}

void termpaint_input_set_event_cb(termpaint_input *ctx, void (*cb)(void *, termpaint_input_event *), void *user_data) {
    ctx->event_cb = cb;
    ctx->event_user_data = user_data;
}

static inline char char_from(int c) {
    return (char)((unsigned char)c);
}

bool termpaint_input_add_data(termpaint_input *ctx, const char *data, unsigned length) {
    if (length + ctx->used >= MAX_SEQ_LENGTH) {
        // bail out
        termpaintp_input_reset(ctx);
        return false;
    }
    // TODO utf8
    for (unsigned i = 0; i < length; i++) {
        ctx->buff[ctx->used] = data[i];
        ++ctx->used;

        bool finished = false;
        bool retrigger = false;
        bool retrigger2 = false; // used in tpis_cmd_str to reprocess "\e[" (last 2 chars)

        switch (ctx->state) {
            case tpis_base:
                // assert(ctx->used == 0);

                // detect valid utf-8 multi char start bytes
                if (0xfc == (0xfe & data[i])) {
                    ctx->state = tpid_utf8_5;
                } else if (0xf8 == (0xfc & data[i])) {
                    ctx->state = tpid_utf8_4;
                } else if (0xf0 == (0xf8 & data[i])) {
                    ctx->state = tpid_utf8_3;
                } else if (0xe0 == (0xf0 & data[i])) {
                    ctx->state = tpid_utf8_2;
                } else if (0xc0 == (0xe0 & data[i])) {
                    ctx->state = tpid_utf8_1;

                // escape sequence starts
                } else if (data[i] == '\e') {
                    ctx->state = tpis_esc;
                } else if (data[i] == char_from(0x8f)) { // SS3
                    ctx->state = tpis_ss3;
                } else if (data[i] == char_from(0x90)) { // DCS
                    ctx->state = tpis_cmd_str;
                } else if (data[i] == char_from(0x9b)) { // CSI
                    ctx->state = tpis_csi;
                } else if (data[i] == char_from(0x9d)) { // OSC
                    ctx->state = tpis_cmd_str;
                } else {
                    finished = true;
                }
                break;
            case tpis_esc:
                if (data[i] == 'O') {
                    ctx->state = tpis_ss3;
                } else if (data[i] == 'P') {
                    ctx->state = tpis_cmd_str;
                } else if (data[i] == '[') {
                    ctx->state = tpis_csi;
                } else if (data[i] == ']') {
                    ctx->state = tpis_cmd_str;
                } else if (0xfc == (0xfe & data[i])) { // meta -> ESC can produce these
                    ctx->state = tpid_utf8_5;
                } else if (0xf8 == (0xfc & data[i])) {
                    ctx->state = tpid_utf8_4;
                } else if (0xf0 == (0xf8 & data[i])) {
                    ctx->state = tpid_utf8_3;
                } else if (0xe0 == (0xf0 & data[i])) {
                    ctx->state = tpid_utf8_2;
                } else if (0xc0 == (0xe0 & data[i])) {
                    ctx->state = tpid_utf8_1;
                } else if (data[i] == '\e') {
                    retrigger = true;
                } else {
                    finished = true;
                }
                break;
            case tpis_ss3:
                // this ss3 stuff is totally undocumented. But various codes
                // are seen in the wild that extend these codes by embedding
                // one digit
                if (data[i] >= '0' && data[i] <= '9') {
                    ;
                } else if (data[i] == '\e') {
                    retrigger = true;
                } else {
                    finished = true;
                }
                break;
            case tpis_csi:
                if (data[i] >= '@' && data[i] <= '~') {
                    finished = true;
                } else if (data[i] == '\e') {
                    retrigger = true;
                }
                break;
            case tpis_cmd_str:
                if (data[i] == '\e') {
                    ctx->state = tpis_str_terminator_esc;
                }
                break;
            case tpis_str_terminator_esc:
                // we expect a '\\' here. But every other char also aborts parsing
                // as a workaround for retriggering:
                if (data[i] == '[') {
                    retrigger2 = true;
                } else {
                    finished = true;
                }
                break;
            case tpid_utf8_5:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    finished = true;
                } else {
                    ctx->state = tpid_utf8_4;
                }
                break;
            case tpid_utf8_4:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    finished = true;
                } else {
                    ctx->state = tpid_utf8_3;
                }
                break;
            case tpid_utf8_3:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    finished = true;
                } else {
                    ctx->state = tpid_utf8_2;
                }
                break;
            case tpid_utf8_2:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    finished = true;
                } else {
                    ctx->state = tpid_utf8_1;
                }
                break;
            case tpid_utf8_1:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    finished = true;
                } else {
                    finished = true;
                }
                break;
        }
        if (finished) {
            termpaintp_input_raw(ctx, ctx->buff, ctx->used);
            termpaintp_input_reset(ctx);
        } else if (retrigger2) {
            // current and previous char is not part of sequence
            termpaintp_input_raw(ctx, ctx->buff, ctx->used - 2);
            termpaintp_input_reset(ctx);
            ctx->buff[ctx->used] = '\e';
            ++ctx->used;
            ctx->buff[ctx->used] = '[';
            ++ctx->used;
            ctx->state = tpis_csi;
        } else if (retrigger) {
            // current char is not part of sequence
            termpaintp_input_raw(ctx, ctx->buff, ctx->used - 1);
            termpaintp_input_reset(ctx);
            --i; // process this char again
        }
    }
    return false;
}



const char *termpaint_input_peek_buffer(termpaint_input *ctx) {
    return ctx->buff;
}


int termpaint_input_peek_buffer_length(termpaint_input *ctx) {
    return ctx->used;
}
