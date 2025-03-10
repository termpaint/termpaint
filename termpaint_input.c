// SPDX-License-Identifier: BSL-1.0
#include "termpaint_input.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h> // for exit
#include <stdio.h> // for debugging prints and debugging data export

#include "termpaint_compiler.h"

#include "termpaint_utf8.h"

/* Known problems:
 *  * Massivly depends on resync trick. Non resync mode currently no longer supported
 *  * in modOther ctrl-? strange (utf 8 converter?)
 *  * needs to detect utf-8 encoded C1 chars? Or maybe that not used in the wild at all?
 */


#ifndef nullptr
#define nullptr ((void*)0)
#endif

#define DEF_ATOM(name, value) \
static const char ATOM_ ## name[] = value; \
const char *termpaint_input_ ## name (void) { return ATOM_ ## name; }


// Naming based on W3C uievents-code spec
DEF_ATOM(enter, "Enter")
DEF_ATOM(space, "Space")
DEF_ATOM(tab, "Tab")
DEF_ATOM(backspace, "Backspace")
DEF_ATOM(context_menu, "ContextMenu")

DEF_ATOM(delete, "Delete")
DEF_ATOM(end, "End")
// help omitted
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

// Misc Events
DEF_ATOM(focus_in, "FocusIn")
DEF_ATOM(focus_out, "FocusOut")

DEF_ATOM(paste_begin, "PasteBegin")
DEF_ATOM(paste_end, "PasteEnd")

DEF_ATOM(i_resync, "i_resync")


#define MOD_CTRL TERMPAINT_MOD_CTRL
#define MOD_ALT TERMPAINT_MOD_ALT
#define MOD_SHIFT TERMPAINT_MOD_SHIFT
#define MOD_ALTGR TERMPAINT_MOD_ALTGR

#define MOD_PRINT (1u << 31)
#define MOD_ENTER (1 << 31 + 1 << 30)
//#define MOD_PRINT (1 << 31)

struct key_mapping_entry_ {
    const char *sequence;
    const char *atom;
    unsigned int modifiers;
};
typedef struct key_mapping_entry_ key_mapping_entry;

#define XTERM_MODS(PREFIX, POSTFIX, ATOM)                           \
    { PREFIX "2" POSTFIX, ATOM,                      MOD_SHIFT },   \
    { PREFIX "3" POSTFIX, ATOM,            MOD_ALT             },   \
    { PREFIX "4" POSTFIX, ATOM,            MOD_ALT | MOD_SHIFT },   \
    { PREFIX "5" POSTFIX, ATOM, MOD_CTRL                       },   \
    { PREFIX "6" POSTFIX, ATOM, MOD_CTRL           | MOD_SHIFT },   \
    { PREFIX "7" POSTFIX, ATOM, MOD_CTRL | MOD_ALT             },   \
    { PREFIX "8" POSTFIX, ATOM, MOD_CTRL | MOD_ALT | MOD_SHIFT }

// xterm has 2 settings where a '>' is added to the CSI sequences added, support that too
// ESC[>2;3m and ESC[>1;3m
#define XTERM_MODS_GT(STR, POSTFIX, ATOM)                                 \
    { "\033["  STR "2" POSTFIX, ATOM,                      MOD_SHIFT },   \
    { "\033[>" STR "2" POSTFIX, ATOM,                      MOD_SHIFT },   \
    { "\033["  STR "3" POSTFIX, ATOM,            MOD_ALT             },   \
    { "\033[>" STR "3" POSTFIX, ATOM,            MOD_ALT             },   \
    { "\033["  STR "4" POSTFIX, ATOM,            MOD_ALT | MOD_SHIFT },   \
    { "\033[>" STR "4" POSTFIX, ATOM,            MOD_ALT | MOD_SHIFT },   \
    { "\033["  STR "5" POSTFIX, ATOM, MOD_CTRL                       },   \
    { "\033[>" STR "5" POSTFIX, ATOM, MOD_CTRL                       },   \
    { "\033["  STR "6" POSTFIX, ATOM, MOD_CTRL           | MOD_SHIFT },   \
    { "\033[>" STR "6" POSTFIX, ATOM, MOD_CTRL           | MOD_SHIFT },   \
    { "\033["  STR "7" POSTFIX, ATOM, MOD_CTRL | MOD_ALT             },   \
    { "\033[>" STR "7" POSTFIX, ATOM, MOD_CTRL | MOD_ALT             },   \
    { "\033["  STR "8" POSTFIX, ATOM, MOD_CTRL | MOD_ALT | MOD_SHIFT },   \
    { "\033[>" STR "8" POSTFIX, ATOM, MOD_CTRL | MOD_ALT | MOD_SHIFT }

// keyboard settings to consider:
// xterm:
//    xterm.vt100.translations: <KeyPress>: insert() --> remove all xterm side keybindings
//    xterm.vt100.modifyCursorKeys \in (-1, 0, 1, 2, 3)
//    xterm.vt100.modifyFunctionKeys \in (-1, 0, 1, 2, 3)
//    xterm.vt100.modifyKeyboard ??
//    xterm.vt100.modifyOtherKeys ??
//    xterm.vt100.oldXtermFKeys ??
//
//    urxvt: urxvt --perl-ext-common "" --perl-ext "" ++iso14755 -keysym.Insert "builtin-string:" -keysym.Prior "builtin-string:" -keysym.Next "builtin-string:" -keysym.C-M-v "builtin-string:" -keysym.C-M-c "builtin-string:"
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

static const key_mapping_entry key_mapping_table[] = {
    { "\x0d", ATOM_enter, 0 }, // also ctrl-m in traditional mode
    { "\033\x0d", ATOM_enter, MOD_ALT },
    XTERM_MODS("\033[27;", ";13~", ATOM_enter), // modifiy other keys mode
    XTERM_MODS("\033[13;", "u", ATOM_enter), // modifiy other keys mode

    { "\x09", ATOM_tab, 0 }, //also ctrl_i
    { "\033\x09", ATOM_tab, MOD_ALT }, //also ctrl-alt-i
    { "\033[Z", ATOM_tab, MOD_SHIFT }, // xterm, normal mode
    XTERM_MODS("\033[27;", ";9~", ATOM_tab), // modifiy other keys mode
    XTERM_MODS("\033[9;", "u", ATOM_tab), // modifiy other keys mode

    { " ", ATOM_space, 0 },
    { "\033 ", ATOM_space, MOD_ALT },
    // { "\x00", ATOM_SPACE, MOD_CTRL } via special case in code
    // { "\033\x00", ATOM_space, MOD_CTRL | MOD_ALT} via special case in code
    XTERM_MODS("\033[27;", ";32~", ATOM_space), // modifiy other keys mode
    XTERM_MODS("\033[32;", "u", ATOM_space), // modifiy other keys mode
    //+ also ctrl-2

    { "\033[29~", ATOM_context_menu, 0 },
    // + also shift F4 in linux vt
    XTERM_MODS("\033[29;", "~", ATOM_context_menu),

    { "\033[3~", ATOM_delete, 0 },
    XTERM_MODS("\033[3;", "~", ATOM_delete),
    { "\033[3$", ATOM_delete, MOD_SHIFT},
    { "\033[3^", ATOM_delete, MOD_CTRL},
    { "\033[3@", ATOM_delete, MOD_CTRL | MOD_SHIFT},
    { "\033\033[3~", ATOM_delete, MOD_ALT },
    { "\033\033[3$", ATOM_delete, MOD_ALT | MOD_SHIFT},
    { "\033\033[3^", ATOM_delete, MOD_CTRL | MOD_ALT},
    { "\033\033[3@", ATOM_delete, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\033[3;1~", ATOM_delete, MOD_ALTGR },
    { "\033[F", ATOM_end, 0},
    XTERM_MODS_GT("1;", "F", ATOM_end),
    { "\033OF", ATOM_end,  0},
    { "\033[4~", ATOM_end, 0},
    { "\033[8~", ATOM_end, 0},
    { "\033[8$", ATOM_end, MOD_SHIFT},
    { "\033[8^", ATOM_end, MOD_CTRL},
    { "\033[8@", ATOM_end, MOD_CTRL | MOD_SHIFT},
    { "\033\033[8~", ATOM_end, MOD_ALT},
    { "\033\033[8$", ATOM_end, MOD_ALT | MOD_SHIFT},
    { "\033\033[8^", ATOM_end, MOD_CTRL | MOD_ALT},
    { "\033\033[8@", ATOM_end, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\033[1;1F", ATOM_end, MOD_ALTGR},
    { "\033[H", ATOM_home, 0},
    XTERM_MODS_GT("1;", "H", ATOM_home),
    { "\033OH", ATOM_home,  0},
    { "\033[1~", ATOM_home, 0},
    { "\033[7~", ATOM_home, 0},
    { "\033[7$", ATOM_home, MOD_SHIFT},
    { "\033[7^", ATOM_home, MOD_CTRL},
    { "\033[7@", ATOM_home, MOD_CTRL | MOD_SHIFT},
    { "\033\033[7~", ATOM_home, MOD_ALT},
    { "\033\033[7$", ATOM_home, MOD_ALT | MOD_SHIFT},
    { "\033\033[7^", ATOM_home, MOD_CTRL | MOD_ALT},
    { "\033\033[7@", ATOM_home, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\033[1;1H", ATOM_home, MOD_ALTGR},
    { "\033[2~", ATOM_insert, 0},
    XTERM_MODS("\033[2;", "~", ATOM_insert),
    { "\033[2$", ATOM_insert, MOD_SHIFT},
    { "\033[2^", ATOM_insert, MOD_CTRL},
    { "\033[2@", ATOM_insert, MOD_CTRL | MOD_SHIFT},
    { "\033\033[2~", ATOM_insert, MOD_ALT},
    { "\033\033[2$", ATOM_insert, MOD_ALT | MOD_SHIFT},
    { "\033\033[2^", ATOM_insert, MOD_CTRL | MOD_ALT},
    { "\033\033[2@", ATOM_insert, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\033[2;1~", ATOM_insert, MOD_ALTGR},
    { "\033[6~", ATOM_page_down, 0},
    XTERM_MODS("\033[6;", "~", ATOM_page_down),
    { "\033[6$", ATOM_page_down, MOD_SHIFT},
    { "\033[6^", ATOM_page_down, MOD_CTRL},
    { "\033[6@", ATOM_page_down, MOD_CTRL | MOD_SHIFT},
    { "\033\033[6~", ATOM_page_down, MOD_ALT},
    { "\033\033[6$", ATOM_page_down, MOD_ALT | MOD_SHIFT},
    { "\033\033[6^", ATOM_page_down, MOD_CTRL | MOD_ALT},
    { "\033\033[6@", ATOM_page_down, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\033[6;1~", ATOM_page_down, MOD_ALTGR},
    { "\033[5~", ATOM_page_up, 0},
    XTERM_MODS("\033[5;", "~", ATOM_page_up), // shift combinations only available when scroll bindings are removed in xterm
    { "\033[5$", ATOM_page_up, MOD_SHIFT},
    { "\033[5^", ATOM_page_up, MOD_CTRL},
    { "\033[5@", ATOM_page_up, MOD_CTRL | MOD_SHIFT},
    { "\033\033[5~", ATOM_page_up, MOD_ALT},
    { "\033\033[5$", ATOM_page_up, MOD_ALT | MOD_SHIFT},
    { "\033\033[5^", ATOM_page_up, MOD_CTRL | MOD_ALT},
    { "\033\033[5@", ATOM_page_up, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\033[5;1~", ATOM_page_up, MOD_ALTGR},

    { "\033[B", ATOM_arrow_down, 0 },
    XTERM_MODS_GT("1;", "B", ATOM_arrow_down),
    { "\033OB", ATOM_arrow_down, 0 },
    { "\033[b", ATOM_arrow_down, MOD_SHIFT },
    { "\033Ob", ATOM_arrow_down, MOD_CTRL },
    { "\033\033[B", ATOM_arrow_down, MOD_ALT },
    { "\033\033[b", ATOM_arrow_down, MOD_ALT | MOD_SHIFT },
    { "\033\033Ob", ATOM_arrow_down, MOD_CTRL | MOD_ALT },
    { "\033[1;1B", ATOM_arrow_down, MOD_ALTGR },
    { "\033[D", ATOM_arrow_left, 0 },
    XTERM_MODS_GT("1;", "D", ATOM_arrow_left),
    { "\033OD", ATOM_arrow_left, 0 },
    { "\033[d", ATOM_arrow_left, MOD_SHIFT },
    { "\033Od", ATOM_arrow_left, MOD_CTRL },
    { "\033\033[D", ATOM_arrow_left, MOD_ALT },
    { "\033\033[d", ATOM_arrow_left, MOD_ALT | MOD_SHIFT },
    { "\033\033Od", ATOM_arrow_left, MOD_CTRL | MOD_ALT },
    { "\033[1;1D", ATOM_arrow_left, MOD_ALTGR },
    { "\033[C", ATOM_arrow_right, 0 },
    XTERM_MODS_GT("1;", "C", ATOM_arrow_right),
    { "\033OC", ATOM_arrow_right, 0 },
    { "\033[c", ATOM_arrow_right, MOD_SHIFT },
    { "\033Oc", ATOM_arrow_right, MOD_CTRL },
    { "\033\033[C", ATOM_arrow_right, MOD_ALT },
    { "\033\033[c", ATOM_arrow_right, MOD_ALT | MOD_SHIFT },
    { "\033\033Oc", ATOM_arrow_right, MOD_CTRL | MOD_ALT },
    { "\033[1;1C", ATOM_arrow_right, MOD_ALTGR },
    { "\033[A", ATOM_arrow_up, 0 },
    XTERM_MODS_GT("1;", "A", ATOM_arrow_up),
    { "\033OA", ATOM_arrow_up, 0 },
    { "\033[a", ATOM_arrow_up, MOD_SHIFT },
    { "\033Oa", ATOM_arrow_up, MOD_CTRL },
    { "\033\033[A", ATOM_arrow_up, MOD_ALT },
    { "\033\033[a", ATOM_arrow_up, MOD_ALT | MOD_SHIFT },
    { "\033\033Oa", ATOM_arrow_up, MOD_CTRL | MOD_ALT },
    { "\033[1;1A", ATOM_arrow_up, MOD_ALTGR },

    { "\033Oo", ATOM_numpad_divide, 0 },
    { "\033\033Oo", ATOM_numpad_divide, MOD_ALT },
    XTERM_MODS("\033O", "o", ATOM_numpad_divide), // ctrl-alt (not shifted) not reachable in xterm
    { "\033Oj", ATOM_numpad_multiply, 0 },
    { "\033\033Oj", ATOM_numpad_multiply, MOD_ALT },
    XTERM_MODS("\033O", "j", ATOM_numpad_multiply), // ctrl-alt (not shifted) not reachable in xterm
    { "\033Om", ATOM_numpad_subtract, 0 },
    { "\033\033Om", ATOM_numpad_subtract, MOD_ALT },
    XTERM_MODS("\033O", "m", ATOM_numpad_subtract), // ctrl-alt (not shifted) not reachable in xterm
    { "\033Ok", ATOM_numpad_add, 0 },
    { "\033\033Ok", ATOM_numpad_add, MOD_ALT },
    XTERM_MODS("\033O", "k", ATOM_numpad_add), // ctrl-alt (not shifted) not reachable in xterm
    { "\033OM", ATOM_numpad_enter, 0 },
    { "\033\033OM", ATOM_numpad_enter, MOD_ALT },
    XTERM_MODS("\033O", "M", ATOM_numpad_enter),
    //{ "\033[3~", ATOM_numpad_decimal, 0 },
    //XTERM_MODS("\033[3;", "~", ATOM_numpad_decimal), // shifted combinations produce other codes in xterm
    { "\033O2l", ATOM_numpad_decimal,                      MOD_SHIFT },
    { "\033O3l", ATOM_numpad_decimal,            MOD_ALT             },
    { "\033O5l", ATOM_numpad_decimal, MOD_CTRL                       },
    { "\033O6l", ATOM_numpad_decimal, MOD_CTRL           | MOD_SHIFT },
    { "\033O4l", ATOM_numpad_decimal,            MOD_ALT | MOD_SHIFT },
    { "\033O7l", ATOM_numpad_decimal, MOD_CTRL | MOD_ALT             },
    { "\033O8l", ATOM_numpad_decimal, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Ol",  ATOM_numpad_decimal,            MOD_ALT             },
    { "\033On",  ATOM_numpad_decimal,                              0 },
    { "\033\033On",ATOM_numpad_decimal,            MOD_ALT             },

    { "\033O2p", ATOM_numpad0,                             MOD_SHIFT },
    { "\033O3p", ATOM_numpad0,                   MOD_ALT             },
    { "\033O5p", ATOM_numpad0,        MOD_CTRL                       },
    { "\033O6p", ATOM_numpad0,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4p", ATOM_numpad0,                   MOD_ALT | MOD_SHIFT },
    { "\033O7p", ATOM_numpad0,        MOD_CTRL | MOD_ALT             },
    { "\033O8p", ATOM_numpad0,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Op",  ATOM_numpad0,                                     0 },
    { "\033\033Op",ATOM_numpad0,                   MOD_ALT             },


    { "\033O2q", ATOM_numpad1,                             MOD_SHIFT },
    { "\033O3q", ATOM_numpad1,                   MOD_ALT             },
    { "\033O5q", ATOM_numpad1,        MOD_CTRL                       },
    { "\033O6q", ATOM_numpad1,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4q", ATOM_numpad1,                   MOD_ALT | MOD_SHIFT },
    { "\033O7q", ATOM_numpad1,        MOD_CTRL | MOD_ALT             },
    { "\033O8q", ATOM_numpad1,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Oq",  ATOM_numpad1,                                     0 },
    { "\033\033Oq",ATOM_numpad1,                   MOD_ALT             },

    { "\033O2r", ATOM_numpad2,                             MOD_SHIFT },
    { "\033O3r", ATOM_numpad2,                   MOD_ALT             },
    { "\033O5r", ATOM_numpad2,        MOD_CTRL                       },
    { "\033O6r", ATOM_numpad2,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4r", ATOM_numpad2,                   MOD_ALT | MOD_SHIFT },
    { "\033O7r", ATOM_numpad2,        MOD_CTRL | MOD_ALT             },
    { "\033O8r", ATOM_numpad2,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Or",  ATOM_numpad2,                                     0 },
    { "\033\033Or",ATOM_numpad2,                   MOD_ALT             },

    { "\033O2s", ATOM_numpad3,                             MOD_SHIFT },
    { "\033O3s", ATOM_numpad3,                   MOD_ALT             },
    { "\033O5s", ATOM_numpad3,        MOD_CTRL                       },
    { "\033O6s", ATOM_numpad3,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4s", ATOM_numpad3,                   MOD_ALT | MOD_SHIFT },
    { "\033O7s", ATOM_numpad3,        MOD_CTRL | MOD_ALT             },
    { "\033O8s", ATOM_numpad3,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Os",  ATOM_numpad3,                                     0 },
    { "\033\033Os",ATOM_numpad3,                   MOD_ALT             },

    { "\033O2t", ATOM_numpad4,                             MOD_SHIFT },
    { "\033O3t", ATOM_numpad4,                   MOD_ALT             },
    { "\033O5t", ATOM_numpad4,        MOD_CTRL                       },
    { "\033O6t", ATOM_numpad4,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4t", ATOM_numpad4,                   MOD_ALT | MOD_SHIFT },
    { "\033O7t", ATOM_numpad4,        MOD_CTRL | MOD_ALT             },
    { "\033O8t", ATOM_numpad4,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Ot",  ATOM_numpad4,                                     0 },
    { "\033\033Ot",ATOM_numpad4,                   MOD_ALT             },

    { "\033O2u", ATOM_numpad5,                             MOD_SHIFT },
    { "\033O3u", ATOM_numpad5,                   MOD_ALT             },
    { "\033O5u", ATOM_numpad5,        MOD_CTRL                       },
    { "\033O6u", ATOM_numpad5,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4u", ATOM_numpad5,                   MOD_ALT | MOD_SHIFT },
    { "\033O7u", ATOM_numpad5,        MOD_CTRL | MOD_ALT             },
    { "\033O8u", ATOM_numpad5,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Ou",  ATOM_numpad5,                                     0 },
    { "\033\033Ou",ATOM_numpad5,                   MOD_ALT             },
    { "\033[E", ATOM_numpad5, 0 },
    XTERM_MODS_GT("1;", "E", ATOM_numpad5),
    { "\033OE", ATOM_numpad5, 0 },
    { "\033[G",  ATOM_numpad5,                                     0 },

    { "\033O2v", ATOM_numpad6,                             MOD_SHIFT },
    { "\033O3v", ATOM_numpad6,                   MOD_ALT             },
    { "\033O5v", ATOM_numpad6,        MOD_CTRL                       },
    { "\033O6v", ATOM_numpad6,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4v", ATOM_numpad6,                   MOD_ALT | MOD_SHIFT },
    { "\033O7v", ATOM_numpad6,        MOD_CTRL | MOD_ALT             },
    { "\033O8v", ATOM_numpad6,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Ov",  ATOM_numpad6,                                     0 },
    { "\033\033Ov",ATOM_numpad6,                   MOD_ALT             },

    { "\033O2w", ATOM_numpad7,                             MOD_SHIFT },
    { "\033O3w", ATOM_numpad7,                   MOD_ALT             },
    { "\033O5w", ATOM_numpad7,        MOD_CTRL                       },
    { "\033O6w", ATOM_numpad7,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4w", ATOM_numpad7,                   MOD_ALT | MOD_SHIFT },
    { "\033O7w", ATOM_numpad7,        MOD_CTRL | MOD_ALT             },
    { "\033O8w", ATOM_numpad7,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Ow",  ATOM_numpad7,                                     0 },
    { "\033\033Ow",ATOM_numpad7,                   MOD_ALT             },

    { "\033O2x", ATOM_numpad8,                             MOD_SHIFT },
    { "\033O3x", ATOM_numpad8,                   MOD_ALT             },
    { "\033O5x", ATOM_numpad8,        MOD_CTRL                       },
    { "\033O6x", ATOM_numpad8,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4x", ATOM_numpad8,                   MOD_ALT | MOD_SHIFT },
    { "\033O7x", ATOM_numpad8,        MOD_CTRL | MOD_ALT             },
    { "\033O8x", ATOM_numpad8,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Ox",  ATOM_numpad8,                                     0 },
    { "\033\033Ox",ATOM_numpad8,                   MOD_ALT             },

    { "\033O2y", ATOM_numpad9,                             MOD_SHIFT },
    { "\033O3y", ATOM_numpad9,                   MOD_ALT             },
    { "\033O5y", ATOM_numpad9,        MOD_CTRL                       },
    { "\033O6y", ATOM_numpad9,        MOD_CTRL           | MOD_SHIFT },
    { "\033O4y", ATOM_numpad9,                   MOD_ALT | MOD_SHIFT },
    { "\033O7y", ATOM_numpad9,        MOD_CTRL | MOD_ALT             },
    { "\033O8y", ATOM_numpad9,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033Oy",  ATOM_numpad9,                                     0 },
    { "\033\033Oy",ATOM_numpad9,                   MOD_ALT             },

    // { "\033", ATOM_escape, }, via special case in code (also Ctrl-[ in traditional mode)
    XTERM_MODS("\033[27;", ";27~", ATOM_escape), // modifiy other keys mode
    XTERM_MODS("\033[27;", "u", ATOM_escape), // modifiy other keys mode
    { "\033\033", ATOM_escape,                                 MOD_ALT },


    { "\033OP", ATOM_f1, 0 },
    XTERM_MODS_GT("1;", "P", ATOM_f1),
    XTERM_MODS("\033O", "P", ATOM_f1),
    { "\033[[A", ATOM_f1, 0 },
    { "\033[25~", ATOM_f1, MOD_SHIFT },
    { "\033[25^", ATOM_f1, MOD_CTRL | MOD_SHIFT },
    { "\033\033[25~", ATOM_f1, MOD_ALT | MOD_SHIFT },
    { "\033\033[25^", ATOM_f1, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033O1P", ATOM_f1, MOD_ALTGR },
    { "\033[11~", ATOM_f1, 0 },
    { "\033[11^", ATOM_f1, MOD_CTRL },
    { "\033\033[11~", ATOM_f1, MOD_ALT },
    { "\033\033[11^", ATOM_f1, MOD_CTRL | MOD_ALT },
    { "\033OQ", ATOM_f2, 0 },
    XTERM_MODS_GT("1;", "Q", ATOM_f2),
    XTERM_MODS("\033O", "Q", ATOM_f2),
    { "\033[[B", ATOM_f2, 0 },
    { "\033[26~", ATOM_f2, MOD_SHIFT },
    { "\033[26^", ATOM_f2, MOD_CTRL | MOD_SHIFT },
    { "\033\033[26~", ATOM_f2, MOD_ALT | MOD_SHIFT },
    { "\033\033[26^", ATOM_f2, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033O1Q", ATOM_f2, MOD_ALTGR },
    { "\033[12~", ATOM_f2, 0 },
    { "\033[12^", ATOM_f2, MOD_CTRL },
    { "\033\033[12~", ATOM_f2, MOD_ALT },
    { "\033\033[12^", ATOM_f2, MOD_CTRL | MOD_ALT },
    { "\033OR", ATOM_f3, 0 },
    XTERM_MODS_GT("1;", "R", ATOM_f3),
    XTERM_MODS("\033O", "R", ATOM_f3),
    { "\033[[C", ATOM_f3, 0 },
    { "\033[28~", ATOM_f3, MOD_SHIFT },
    { "\033[28^", ATOM_f3, MOD_CTRL | MOD_SHIFT },
    { "\033\033[28~", ATOM_f3, MOD_ALT | MOD_SHIFT },
    { "\033\033[28^", ATOM_f3, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033O1R", ATOM_f3, MOD_ALTGR },
    { "\033[13~", ATOM_f3, 0 },
    { "\033[13^", ATOM_f3, MOD_CTRL },
    { "\033\033[13~", ATOM_f3, MOD_ALT },
    { "\033\033[13^", ATOM_f3, MOD_CTRL | MOD_ALT },
    { "\033OS", ATOM_f4, 0 },
    XTERM_MODS_GT("1;", "S", ATOM_f4),
    XTERM_MODS("\033O", "S", ATOM_f4),
    { "\033[[D", ATOM_f4, 0 },
    { "\033O1S", ATOM_f4, MOD_ALTGR },
    { "\033[14~", ATOM_f4, 0 },
    { "\033[14^", ATOM_f4, MOD_CTRL },
    { "\033\033[14~", ATOM_f4, MOD_ALT },
    { "\033\033[14^", ATOM_f4, MOD_CTRL | MOD_ALT },
    { "\033[29^", ATOM_f4, MOD_CTRL | MOD_SHIFT },
    { "\033\033[29~", ATOM_f4, MOD_ALT | MOD_SHIFT },
    { "\033\033[29^", ATOM_f4, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033[15~", ATOM_f5, 0 },
    { "\033[15^", ATOM_f5, MOD_CTRL },
    { "\033\033[15~", ATOM_f5, MOD_ALT },
    { "\033\033[15^", ATOM_f5, MOD_CTRL | MOD_ALT },
    XTERM_MODS_GT("15;", "~", ATOM_f5),
    { "\033[[E", ATOM_f5, 0 },
    { "\033[31~", ATOM_f5, MOD_SHIFT },
    { "\033[31^", ATOM_f5, MOD_CTRL | MOD_SHIFT },
    { "\033\033[31~", ATOM_f5, MOD_ALT | MOD_SHIFT },
    { "\033\033[31^", ATOM_f5, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033[15;1~", ATOM_f5, MOD_ALTGR },
    { "\033[17~", ATOM_f6, 0 },
    { "\033[17^", ATOM_f6, MOD_CTRL },
    { "\033\033[17~", ATOM_f6, MOD_ALT },
    { "\033\033[17^", ATOM_f6, MOD_CTRL | MOD_ALT },
    XTERM_MODS_GT("17;", "~", ATOM_f6),
    { "\033[32~", ATOM_f6, MOD_SHIFT },
    { "\033[32^", ATOM_f6, MOD_CTRL | MOD_SHIFT },
    { "\033\033[32~", ATOM_f6, MOD_ALT | MOD_SHIFT },
    { "\033\033[32^", ATOM_f6, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033[17;1~", ATOM_f6, MOD_ALTGR },
    { "\033[18~", ATOM_f7, 0 },
    { "\033[18^", ATOM_f7, MOD_CTRL },
    { "\033\033[18~", ATOM_f7, MOD_ALT },
    { "\033\033[18^", ATOM_f7, MOD_CTRL | MOD_ALT },
    XTERM_MODS_GT("18;", "~", ATOM_f7),
    { "\033[33~", ATOM_f7, MOD_SHIFT },
    { "\033[33^", ATOM_f7, MOD_CTRL | MOD_SHIFT },
    { "\033\033[33~", ATOM_f7, MOD_ALT | MOD_SHIFT },
    { "\033\033[33^", ATOM_f7, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033[18;1~", ATOM_f7, MOD_ALTGR },
    { "\033[19~", ATOM_f8, 0 },
    { "\033[19^", ATOM_f8, MOD_CTRL },
    { "\033\033[19~", ATOM_f8, MOD_ALT },
    { "\033\033[19^", ATOM_f8, MOD_CTRL | MOD_ALT },
    XTERM_MODS_GT("19;", "~", ATOM_f8),
    { "\033[34~", ATOM_f8, MOD_SHIFT },
    { "\033[34^", ATOM_f8, MOD_CTRL | MOD_SHIFT },
    { "\033\033[34~", ATOM_f8, MOD_ALT | MOD_SHIFT },
    { "\033\033[34^", ATOM_f8, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\033[19;1~", ATOM_f8, MOD_ALTGR },
    { "\033[20~", ATOM_f9, 0 },
    { "\033[20^", ATOM_f9, MOD_CTRL },
    { "\033\033[20~", ATOM_f9, MOD_ALT },
    { "\033\033[20^", ATOM_f9, MOD_CTRL | MOD_ALT },
    XTERM_MODS_GT("20;", "~", ATOM_f9),
    { "\033[20;1~", ATOM_f9, MOD_ALTGR },
    { "\033[21~", ATOM_f10, 0 },
    { "\033[21^", ATOM_f10, MOD_CTRL },
    { "\033\033[21~", ATOM_f10, MOD_ALT },
    { "\033\033[21^", ATOM_f10, MOD_CTRL | MOD_ALT },
    XTERM_MODS_GT("21;", "~", ATOM_f10),
    { "\033[21;1~", ATOM_f10, MOD_ALTGR },
    { "\033[23~", ATOM_f11, 0 },
    { "\033[23$", ATOM_f11, MOD_SHIFT },
    { "\033[23^", ATOM_f11, MOD_CTRL },
    { "\033[23@", ATOM_f11, MOD_CTRL | MOD_SHIFT },
    { "\033\033[23~", ATOM_f11, MOD_ALT },
    { "\033\033[23$", ATOM_f11, MOD_ALT | MOD_SHIFT },
    { "\033\033[23^", ATOM_f11, MOD_CTRL | MOD_ALT },
    { "\033\033[23@", ATOM_f11, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    XTERM_MODS_GT("23;", "~", ATOM_f11),
    { "\033[23;1~", ATOM_f11, MOD_ALTGR },
    { "\033[24~", ATOM_f12, 0 },
    { "\033[24$", ATOM_f12, MOD_SHIFT },
    { "\033[24^", ATOM_f12, MOD_CTRL },
    { "\033[24@", ATOM_f12, MOD_CTRL | MOD_SHIFT },
    { "\033\033[24~", ATOM_f12, MOD_ALT },
    { "\033\033[24$", ATOM_f12, MOD_ALT | MOD_SHIFT },
    { "\033\033[24^", ATOM_f12, MOD_CTRL | MOD_ALT },
    { "\033\033[24@", ATOM_f12, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    XTERM_MODS_GT("24;", "~", ATOM_f12),
    { "\033[24;1~", ATOM_f12, MOD_ALTGR },


    //{ "", ATOM_, 0 },
    //{ "", ATOM_, 0 },
    //{ "", ATOM_, 0 },

    { "\x01",   "a", MOD_CTRL |           MOD_PRINT },
    { "\033\x01", "a", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x02",   "b", MOD_CTRL |           MOD_PRINT },
    { "\033\x02", "b", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x03",   "c", MOD_CTRL |           MOD_PRINT },
    { "\033\x03", "c", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x04",   "d", MOD_CTRL |           MOD_PRINT },
    { "\033\x04", "d", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x05",   "e", MOD_CTRL |           MOD_PRINT },
    { "\033\x05", "e", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x06",   "f", MOD_CTRL |           MOD_PRINT },
    { "\033\x06", "f", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x07",   "g", MOD_CTRL |           MOD_PRINT },
    { "\033\x07", "g", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //{ "\x08",   "h", MOD_CTRL |           MOD_PRINT },
    //+ also ctrl-Backspace
    //{ "\033\x08", "h", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also ctrl-alt-Backspace (which might not be usable as xorg binds zap to it)
    //{ "\x09",   "i",                      MOD_PRINT },
    //+ also Tab, Ctrl-Tab
    //{ "\033\x09", "i",            MOD_ALT | MOD_PRINT },
    //+ also Alt-Tab
    { "\x0a",   "j", MOD_CTRL |           MOD_PRINT },
    { "\033\x0a", "j", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x0b",   "k", MOD_CTRL |           MOD_PRINT },
    { "\033\x0b", "k", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x0c",   "l", MOD_CTRL |           MOD_PRINT },
    { "\033\x0c", "l", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //{ "\x0d",   "m", MOD_CTRL |           MOD_PRINT },
    //+ also Return, Ctrl-Return
    //{ "\033\x0d", "m", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also alt-Return, alt-Ctrl-Return
    { "\x0e",   "n", MOD_CTRL |           MOD_PRINT },
    { "\033\x0e", "n", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x0f",   "o", MOD_CTRL |           MOD_PRINT },
    { "\033\x0f", "o", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x10",   "p", MOD_CTRL |           MOD_PRINT },
    { "\033\x10", "p", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x11",   "q", MOD_CTRL |           MOD_PRINT },
    { "\033\x11", "q", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x12",   "r", MOD_CTRL |           MOD_PRINT },
    { "\033\x12", "r", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x13",   "s", MOD_CTRL |           MOD_PRINT },
    { "\033\x13", "s", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x14",   "t", MOD_CTRL |           MOD_PRINT },
    { "\033\x14", "t", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x15",   "u", MOD_CTRL |           MOD_PRINT },
    { "\033\x15", "u", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x16",   "v", MOD_CTRL |           MOD_PRINT },
    { "\033\x16", "v", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x17",   "w", MOD_CTRL |           MOD_PRINT },
    { "\033\x17", "w", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x18",   "x", MOD_CTRL |           MOD_PRINT },
    { "\033\x18", "x", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x19",   "y", MOD_CTRL |           MOD_PRINT },
    { "\033\x19", "y", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x1a",   "z", MOD_CTRL |           MOD_PRINT },
    { "\033\x1a", "z", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //{ "\x1b",   "[", MOD_CTRL |           MOD_PRINT },
    //+ also ESC
    //+ also ctrl-3
    { "\x1c",   "\\", MOD_CTRL |           MOD_PRINT },
    { "\033\x1c", "\\", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also ctrl-4
    { "\x1d",   "]", MOD_CTRL |           MOD_PRINT },
    //+ also ctrl-5
    { "\033\x1d", "]", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also alt-ctrl-5
    { "\x1e",   "~", MOD_CTRL |           MOD_PRINT },
    //+ also ctrl-6
    { "\033\x1e", "~", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also alt-ctrl-6
    { "\x1f",   "?", MOD_CTRL |           MOD_PRINT },
    { "\033\x1f", "?", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also ctrl-7
    { "\x7f", ATOM_backspace, 0 },
    { "\x08", ATOM_backspace, MOD_CTRL },
    { "\033\x08", ATOM_backspace, MOD_CTRL | MOD_ALT },
    { "\033\x7f", ATOM_backspace, MOD_ALT },
    XTERM_MODS("\033[27;", ";127~", ATOM_backspace), // modifiy other keys mode
    XTERM_MODS("\033[127;", "u", ATOM_backspace), // modifiy other keys mode
    XTERM_MODS("\033[27;", ";8~", ATOM_backspace), // modifiy other keys mode
    XTERM_MODS("\033[8;", "u", ATOM_backspace), // modifiy other keys mode

    { 0, 0, 0 }
};


void termpaintp_input_selfcheck(void) {
    static bool finished;
    if (finished) return;
    bool ok = true;
    for (const key_mapping_entry* entry_a = key_mapping_table; entry_a->sequence != nullptr; entry_a++) {
        for (const key_mapping_entry* entry_b = entry_a; entry_b->sequence != nullptr; entry_b++) {
            if (entry_a != entry_b && strcmp(entry_a->sequence, entry_b->sequence) == 0) {
                printf("Duplicate key mapping: %s == %s\n", entry_a->atom, entry_b->atom);
                ok = false;
            }
        }
    }
    if (!ok) {
        exit(55);
    }
    finished = true;
}

void termpaintp_input_dump_table(void) {
    FILE * f = fopen("input.dump", "w");
    if (!f) {
        abort();
    }
    for (const key_mapping_entry* entry_a = key_mapping_table; entry_a->sequence != nullptr; entry_a++) {
        fputs(entry_a->sequence, f);
        fputs("\n", f);
    }
    fclose(f);
}

#define MAX_SEQ_LENGTH 1024

enum termpaint_input_state {
    tpis_base,
    tpis_esc,
    tpis_ss3,
    tpis_csi,
    tpis_cmd_str,
    tpis_cmd_str_c1,
    tpis_str_terminator_esc,
    tpid_utf8_5, tpid_utf8_4, tpid_utf8_3, tpid_utf8_2, tpid_utf8_1,
    tpis_mouse_btn, tpis_mouse_col, tpis_mouse_row
};

struct termpaint_input_ {
    unsigned char buff[MAX_SEQ_LENGTH];
    int used;
    enum termpaint_input_state state;
    _Bool overflow;
    _Bool esc_pending;

    int expect_cursor_position_report;
    _Bool expect_mouse_char_mode;
    _Bool expect_mouse_multibyte_mode;
    _Bool expect_apc;

    _Bool in_paste;
    _Bool handle_paste;

    int quirks_len;
    key_mapping_entry *quirks;

    _Bool extended_unicode;

    _Bool (*raw_filter_cb)(void *user_data, const char *data, unsigned length, _Bool overflow);
    void *raw_filter_user_data;

    void (*event_cb)(void *, termpaint_event *);
    void *event_user_data;
};



static void termpaintp_input_reset(termpaint_input *ctx) {
    ctx->used = 0;
    ctx->overflow = 0;
    ctx->state = tpis_base;
}

static bool termpaintp_input_checked_append_digit(int *to_update, int base, int value) {
    int tmp;
    if (termpaint_smul_overflow(*to_update, base, &tmp)) {
        return false;
    }
    if (termpaint_sadd_overflow(tmp, value, to_update)) {
        return false;
    }
    return true;
}

static bool termpaintp_input_parse_mb_3(const unsigned char *data, size_t length, int *a, int *b, int *c) {
    if (length < 3) { // three values -> at least 3 bytes
        return false;
    }
    const size_t len_a = termpaintp_utf8_len(data[0]);
    if (len_a >= length // including first byte of b
            || !termpaintp_check_valid_sequence(data, len_a)) {
        return false;
    }
    *a = termpaintp_utf8_decode_from_utf8(data, len_a);

    const size_t len_b = termpaintp_utf8_len(data[len_a]);
    if (len_a + len_b >= length // including first byte of c
            || !termpaintp_check_valid_sequence(data + len_a, len_b)) {
        return false;
    }
    *b = termpaintp_utf8_decode_from_utf8(data + len_a, len_b);

    const size_t len_c = termpaintp_utf8_len(data[len_a + len_b]);
    if (len_a + len_b + len_c != length // don't allow trailing garbage
            || !termpaintp_check_valid_sequence(data + len_a + len_b, len_c)) {
        return false;
    }
    *c = termpaintp_utf8_decode_from_utf8(data + len_a + len_b, len_c);
    return true;
}

static void termpaintp_input_translate_mouse_flags(termpaint_event* event, int mode) {
    // mode = 0 -> release if button is 3 (all modes except 1006)
    // mode = 1 -> release from final (mode 1006 with 'm' as final)
    // mode = 2 -> press from final (mode 1006 with 'M' as final)

    // shuffle the bits from the raw button and flags
    event->mouse.button = event->mouse.raw_btn_and_flags & 0x3;
    if (event->mouse.raw_btn_and_flags & 0x40) {
        event->mouse.button |= 4;
    }
    if (event->mouse.raw_btn_and_flags & 0x80) {
        event->mouse.button |= 8;
    }

    event->mouse.modifier = 0;
    if (event->mouse.raw_btn_and_flags & 0x4) {
        event->mouse.modifier |= TERMPAINT_MOD_SHIFT;
    }
    if (event->mouse.raw_btn_and_flags & 0x8) {
        event->mouse.modifier |= TERMPAINT_MOD_ALT;
    }
    if (event->mouse.raw_btn_and_flags & 0x10) {
        event->mouse.modifier |= TERMPAINT_MOD_CTRL;
    }

    if (event->mouse.raw_btn_and_flags & 0x20) {
        event->mouse.action = TERMPAINT_MOUSE_MOVE;
    } else if (mode == 0) {
        event->mouse.action = event->mouse.button != 3 ? TERMPAINT_MOUSE_PRESS : TERMPAINT_MOUSE_RELEASE;
    } else if (mode == 1) {
        event->mouse.action = TERMPAINT_MOUSE_RELEASE;
    } else {
        event->mouse.action = TERMPAINT_MOUSE_PRESS;
    }
}

static void termpaintp_input_raw(termpaint_input *ctx, const unsigned char *data, size_t length, _Bool overflow) {
    unsigned char dbl_esc_tmp[21];
    // First handle double escape for alt-ESC
    if (overflow) {
        // overflow just reset to base state.
        ctx->esc_pending = false;
    } else {
        if (!ctx->esc_pending) {
            if (length == 1 && data[0] == '\033') {
                // skip processing this, either next key or resync will trigger real handling
                ctx->esc_pending = true;
                return;
            }
        } else {
            ctx->esc_pending = false;

            bool found = false;

            if (length + 1 < sizeof (dbl_esc_tmp)) {
                dbl_esc_tmp[0] = '\033';
                memcpy(dbl_esc_tmp + 1, data, length);
                for (const key_mapping_entry* entry = key_mapping_table; entry->sequence != nullptr; entry++) {
                    if (strlen(entry->sequence) == length + 1 && memcmp(entry->sequence, dbl_esc_tmp, length + 1) == 0) {
                        found = true;
                    }
                }
            }

            if (found) {
                // alt-<Something>, this is just one event
                length += 1;
                data = dbl_esc_tmp;
            } else {
                // something else, two events
                if (ctx->raw_filter_cb && ctx->raw_filter_cb(ctx->raw_filter_user_data, (const char *)"\033", 1, false)) {
                    ; // skipped by raw filter
                } else if (ctx->event_cb) {
                    termpaint_event event;
                    event.type = TERMPAINT_EV_KEY;
                    event.key.length = strlen(ATOM_escape);
                    event.key.atom = ATOM_escape;
                    event.key.modifier = 0;
                    ctx->event_cb(ctx->event_user_data, &event);
                }
            }
        }
    }

    if (ctx->raw_filter_cb) {
        if (ctx->raw_filter_cb(ctx->raw_filter_user_data, (const char *)data, length, overflow)) {
            return;
        }
    }
    if (!ctx->event_cb) {
        return;
    }

    unsigned char buffer[6];

    termpaint_event event;
    event.type = 0;
    if (overflow) {
        event.type = TERMPAINT_EV_OVERFLOW;
        /*event.length = 0;
        event.atom_or_string = 0;
        event.modifier = 0;*/
    } else if (length == 0) {
        // length == 0 should only be possible with overflow. Bailing here removes some conditions later.
        return;
    } else if (length == 1 && data[0] == 0) {
        event.type = TERMPAINT_EV_KEY;
        event.key.length = strlen(ATOM_space);
        event.key.atom = ATOM_space;
        event.key.modifier = MOD_CTRL;
    } else if (length == 2 && data[0] == '\033' && data[1] == 0) {
        event.type = TERMPAINT_EV_KEY;
        event.key.length = strlen(ATOM_space);
        event.key.atom = ATOM_space;
        event.key.modifier = MOD_CTRL | MOD_ALT;
    } else {
        const key_mapping_entry* matched_entry = nullptr;

        for (int i = 0; i < ctx->quirks_len; i++) {
            const key_mapping_entry* entry = &ctx->quirks[i];
            if (strlen(entry->sequence) == length && memcmp(entry->sequence, data, length) == 0) {
                matched_entry = entry;
                break;
            }
        }

        // TODO optimize
        if (!matched_entry) {
            for (const key_mapping_entry* entry = key_mapping_table; entry->sequence != nullptr; entry++) {
                if (strlen(entry->sequence) == length && memcmp(entry->sequence, data, length) == 0) {
                    matched_entry = entry;
                    break;
                }
            }
        }
        if (matched_entry) {
            if (matched_entry->modifiers & MOD_PRINT) {
                // special case for ctrl-X which is in the table but a modified printable
                event.type = TERMPAINT_EV_CHAR;
                event.c.length = strlen(matched_entry->atom);
                event.c.string = matched_entry->atom;
                event.c.modifier = matched_entry->modifiers & ~MOD_PRINT;
            } else {
                event.type = TERMPAINT_EV_KEY;
                event.key.length = strlen(matched_entry->atom);
                event.key.atom = matched_entry->atom;
                event.key.modifier = matched_entry->modifiers;
            }
        }
        if (length == 4 && data[0] == '\033' && data[1] == '[' && data[2] == '0' && data[3] == 'n') {
            event.type = TERMPAINT_EV_MISC;
            event.misc.atom = ATOM_i_resync;
            event.misc.length = strlen(ATOM_i_resync);
        }

        if (!event.type && length >= 2 && data[0] == '\033' && (0xc0 == (0xc0 & data[1]))) {
            // tokenizer can only abort on invalid utf-8 sequences, so now recheck and issue a distinct event type
            event.type = termpaintp_check_valid_sequence(data+1, length - 1) ? TERMPAINT_EV_CHAR : TERMPAINT_EV_INVALID_UTF8;
            if (length - 1 > 4 && !ctx->extended_unicode) {
                event.type = TERMPAINT_EV_INVALID_UTF8;
            }
            event.c.length = length-1;
            event.c.string = (const char*)data+1;
            event.c.modifier = MOD_ALT;
        }
        if (!event.type && length == 2 && data[0] == '\033' && data[1] > 32 && data[1] < 127) {
            event.type = TERMPAINT_EV_CHAR;
            event.c.length = length-1;
            event.c.string = (const char*)data+1;
            event.c.modifier = MOD_ALT;
        }
        if (!event.type && (0xc0 == (0xc0 & data[0]))) {
            // tokenizer can only abort on invalid utf-8 sequences, so now recheck and issue a distinct event type
            event.type = termpaintp_check_valid_sequence(data, length) ? TERMPAINT_EV_CHAR : TERMPAINT_EV_INVALID_UTF8;
            if (length > 4 && !ctx->extended_unicode) {
                event.type = TERMPAINT_EV_INVALID_UTF8;
            }
            event.c.length = length;
            event.c.string = (const char*)data;
            event.c.modifier = 0;
        }
        if (!event.type && length == 1 && data[0] > 32 && data[0] < 127) {
            event.type = TERMPAINT_EV_CHAR;
            event.c.length = length;
            event.c.string = (const char*)data;
            event.c.modifier = 0;
        }

        if (length > 2 && data[0] == '\033' && data[1] == '[') {
            int params_start = 2;
            int params_len = length - 3;

            // scan for shape
            char prefix_modifier = 0;
            char postfix_modifier = 0;
            char final = 0;

            const int default_arg = -1;
            int args[10] = { 0 };
            bool has_sub_args = false;
            const int max_args = sizeof(args) / sizeof(*args);
            int arg_count = 0;

            enum state_t {
                S_initial, S_main_param, S_sub_param, S_ignore
            } state = S_initial;

            bool ok = true;
            for (size_t j = 2; j < length; j++) {
                if ('0' <= data[j] && data[j] <= '9') {
                    if (state == S_initial) {
                        if (arg_count >= max_args) {
                            state = S_ignore;
                        } else {
                            state = S_main_param;
                            ++arg_count;
                        }
                    }
                    if (state == S_main_param) {
                        if (!termpaintp_input_checked_append_digit(&args[arg_count - 1], 10, data[j] - '0')) {
                            // parameter out of range
                            state = S_ignore;
                            ok = false;
                        }
                    }
                } else if (':' == data[j]) {
                    has_sub_args = true;
                    if (state == S_initial) {
                        if (arg_count >= max_args) {
                            state = S_ignore;
                        } else {
                            ++arg_count;
                            args[arg_count - 1] = default_arg;
                            state = S_sub_param;
                        }
                    } else if (state == S_main_param) {
                        state = S_sub_param;
                    }
                } else if (data[j] == ';') {
                    if (state == S_initial) {
                        if (arg_count >= max_args) {
                            state = S_ignore;
                        } else {
                            ++arg_count;
                            args[arg_count - 1] = default_arg;
                            state = S_initial;
                        }
                    } else if (state == S_main_param) {
                        state = S_initial;
                    } else if (state == S_sub_param) {
                        state = S_initial;
                    }
                } else if ('<' <= data[j] && data[j] <= '?') {
                    // prefix modifier
                    if (j == 2) {
                        // at the very beginning
                        prefix_modifier = data[j];
                        ++params_start;
                        --params_len;
                    } else {
                        // at an unexpected place
                        ok = false;
                        break;
                    }
                } else if (' ' <= data[j] && data[j] <= '/') {
                    // postfix modifier
                    if (j == length - 2) {
                        // just before final character
                        postfix_modifier = data[j];
                        --params_len;
                    } else {
                        // at an unexpected place
                        ok = false;
                        break;
                    }
                } else if ('@' <= data[j] && data[j] <= 127) {
                    // final character
                    if (j == length - 1) {
                        // and actually in the final byte
                        final = data[j];
                    } else {
                        // at an unexpected place
                        ok = false;
                        break;
                    }
                } else {
                    ok = false;
                    break;
                }
            }

            if (state == S_initial && arg_count > 0 && arg_count < max_args) {
                ++arg_count;
                args[arg_count - 1] = default_arg;
            }

#define SEQ(f, pre, post) (((pre) << 16) | ((post) << 8) | (f))
            int32_t sequence_id = ok ? SEQ(final, prefix_modifier, postfix_modifier) : 0;

            // the CSI sequence is just a prefix in legacy mouse modes.
            if (!event.type && length >= 6 && data[2] == 'M') {
                if (length == 6) {
                    if (data[3] >= 32 && data[4] > 32 && data[5] > 32) {
                        // only translate non overflow mouse reports (some terminals overflow into the C0 range,
                        // ignore those too)
                        event.type = TERMPAINT_EV_MOUSE;
                        event.mouse.raw_btn_and_flags = data[3] - ' ';
                        event.mouse.x = data[4] - '!';
                        event.mouse.y = data[5] - '!';
                        termpaintp_input_translate_mouse_flags(&event, 0);
                    }
                } else {
                    int x, y, btn;
                    if (termpaintp_input_parse_mb_3(data + 3, length - 3, &btn, &x, &y)) {
                        if (btn >= 32 && x > 32 && y > 32) {
                            // here no overflow should be possible. But the substractions would yield negative values
                            // otherwise
                            event.mouse.raw_btn_and_flags = btn - ' ';
                            event.mouse.x = x - '!';
                            event.mouse.y = y - '!';
                            termpaintp_input_translate_mouse_flags(&event, 0);
                            event.type = TERMPAINT_EV_MOUSE;
                        }
                    }
                }
            }

            if (!event.type && sequence_id == SEQ('M', 0, 0) && length > 7) {
                // urxvt mouse mode 1015
                if (arg_count == 3 && !has_sub_args) {
                    int btn = args[0];
                    int x = args[1];
                    int y = args[2];
                    if (btn >= ' ' && x > 0 && y > 0) {
                        event.type = TERMPAINT_EV_MOUSE;
                        event.mouse.raw_btn_and_flags = btn - ' ';
                        event.mouse.x = x - 1;
                        event.mouse.y = y - 1;
                        termpaintp_input_translate_mouse_flags(&event, 0);
                    }
                }
            }

            if (!event.type && length > 8 && (sequence_id == SEQ('M', '<', 0) || sequence_id == SEQ('m', '<', 0))) {
                // mouse mode 1006
                if (arg_count == 3 && !has_sub_args) {
                    int btn = (args[0] != default_arg ? args[0] : 0);
                    int x = args[1];
                    int y = args[2];
                    if (x > 0 && y > 0) {
                        event.type = TERMPAINT_EV_MOUSE;
                        event.mouse.raw_btn_and_flags = btn;
                        event.mouse.x = x - 1;
                        event.mouse.y = y - 1;
                        termpaintp_input_translate_mouse_flags(&event, data[length - 1] == 'm' ? 1 : 2);
                    }
                }
            }

            // the nice key modifier extensions:
            // \033[27;<mod>;<char>~
            // \033[<char>;<mod>u
            if (!event.type
                    && ((sequence_id == SEQ('~', 0, 0) && arg_count >= 3 && args[0] == 27)
                        || (sequence_id == SEQ('u', 0, 0) && arg_count >= 2 && !has_sub_args))) {
                // see further down for other CSI ~ sequences
                int mod, codepoint;

                if (sequence_id == SEQ('u', 0, 0)) {
                    codepoint = args[0];
                    mod = args[1];
                } else {
                    // ~ variant
                    mod = args[1];
                    codepoint = args[2];
                }

                if (mod == 0) {
                    // This does not happen in tested terminals but
                    // https://www.leonerd.org.uk/hacks/fixterms/
                    // implies we should just default to 1
                    mod = 1;
                }
                if (codepoint >= 32 && codepoint <= 0x7FFFFFFF
                        && !(codepoint >= 0x80 && codepoint <= 0xa0)
                        && codepoint != 0x7f) {
                    event.type = TERMPAINT_EV_CHAR;
                    event.c.length = termpaintp_encode_to_utf8(codepoint, buffer);
                    event.c.string = (char*)buffer;
                    event.c.modifier = 0;
                    mod = mod - 1;
                    if (mod & 1) {
                        event.c.modifier |= MOD_SHIFT;
                    }
                    if (mod & 2) {
                        event.c.modifier |= MOD_ALT;
                    }
                    if (mod & 4) {
                        event.c.modifier |= MOD_CTRL;
                    }
                }
            }

            if ((!event.type || ctx->expect_cursor_position_report > 0)
                    && length > 5 && (sequence_id == SEQ('R', 0, 0) || sequence_id == SEQ('R', '?', 0))) {
                if (arg_count >= 2 && !has_sub_args) {
                    int y = args[0];
                    int x = args[1];

                    if (x > 0 && y > 0) {
                        event.type = TERMPAINT_EV_CURSOR_POSITION;
                        event.cursor_position.x = x - 1;
                        event.cursor_position.y = y - 1;
                        if (prefix_modifier == 0) {
                            ctx->expect_cursor_position_report -= 1;
                        }
                        event.cursor_position.safe = prefix_modifier == '?';
                    }
                }
            }

            if (!event.type && params_len == 0
                    && (sequence_id == SEQ('O', 0, 0) || sequence_id == SEQ('I', 0, 0))) {
                event.type = TERMPAINT_EV_MISC;
                event.misc.atom = sequence_id == SEQ('I', 0, 0) ? termpaint_input_focus_in()
                                                                  : termpaint_input_focus_out();
                event.misc.length = strlen(event.misc.atom);
            }

            if (!event.type && sequence_id == SEQ('~', 0, 0)) { // see above for CSI 27;<mod>;<char>~
                if (arg_count >= 1 && !has_sub_args) {
                    int num = args[0];
                    if (num == 200) {
                        if (ctx->handle_paste) {
                            ctx->in_paste = true;
                            termpaint_event event2;
                            event2.type = TERMPAINT_EV_PASTE;
                            event2.paste.string = "";
                            event2.paste.length = 0;
                            event2.paste.initial = true;
                            event2.paste.final = false;
                            ctx->event_cb(ctx->event_user_data, &event2);
                        } else {
                            event.type = TERMPAINT_EV_MISC;
                            event.misc.atom = termpaint_input_paste_begin();
                            event.misc.length = strlen(event.misc.atom);
                        }
                    } else if (num == 201) {
                        if (ctx->handle_paste) {
                            ctx->in_paste = false;
                            event.type = TERMPAINT_EV_PASTE;
                            event.paste.string = "";
                            event.paste.length = 0;
                            event.paste.initial = false;
                            event.paste.final = true;
                        } else {
                            event.type = TERMPAINT_EV_MISC;
                            event.misc.atom = termpaint_input_paste_end();
                            event.misc.length = strlen(event.misc.atom);
                        }
                    }
                }
            }

            if (!event.type) {
                if (length > 5 &&
                        (sequence_id == SEQ('y', 0, '$') || sequence_id == SEQ('y', '?', '$'))) {
                    if (arg_count >= 2 && !has_sub_args) {
                        int mode = (args[0] != default_arg ? args[0] : 0);
                        int status = (args[1] != default_arg ? args[1] : 0);
                        event.type = TERMPAINT_EV_MODE_REPORT;
                        event.mode.number = mode;
                        event.mode.kind = (prefix_modifier == '?') ? 1 : 0;
                        event.mode.status = status;
                    }
                }

                if (sequence_id == SEQ('c', '>', 0)) {
                    event.type = TERMPAINT_EV_RAW_SEC_DEV_ATTRIB;
                    event.raw.string = (const char*)data;
                    event.raw.length = length;
                }

                if (sequence_id == SEQ('c', '?', 0)) {
                    event.type = TERMPAINT_EV_RAW_PRI_DEV_ATTRIB;
                    event.raw.string = (const char*)data;
                    event.raw.length = length;
                }

                // prefix_modifier == '?' is possible here, VTE < 0.54 answers this to CSI 1x
                if (sequence_id == SEQ('x', 0, 0) || sequence_id == SEQ('x', '?', 0)) {
                    event.type = TERMPAINT_EV_RAW_DECREQTPARM;
                    event.raw.string = (const char*)data;
                    event.raw.length = length;
                }
            }
#undef SEQ
        }

        if (!event.type && length > 5 && data[0] == '\033' && data[1] == ']' &&
                ((data[length-1] == '\\' && data[length-2] == '\033') || (data[length-1] == '\x07') || (data[length-1] == 0x9c))) {
            // OSC sequences
            size_t st_offset;
            if (data[length-1] == '\\') {
                st_offset = length - 2;
            } else {
                st_offset = length - 1;
            }
            int num; // -1 -> not a numerical OSC
            size_t num_end = 0;
            if ('0' <= data[2] && data[2] <= '9') {
                num = 0;
                for (size_t i = 2; i < st_offset; i++) {
                    if (data[i] == ';') {
                        num_end = i;
                        // finished
                        break;
                    } else if ('0' <= data[i] && data[i] <= '9') {
                        if (!termpaintp_input_checked_append_digit(&num, 10, data[i] - '0')) {
                            num = -1;
                            break;
                        }
                    } else {
                        // bail
                        num = -1;
                        break;
                    }
                }
            } else {
                num = -1;
            }

            if (num_end && num == 4) {
                event.palette_color_report.color_index = 0;
                // the normal report has the form OSC 4 ; color_index ; color_desc ST
                // but at least urxvt does send an different form of OSC 4 ; color_desc ST
                //    (set color_index == -1 in the event in that case)
                bool color_index_ok = true;
                size_t end_idx1 = num_end + 1;
                while (end_idx1 < st_offset && data[end_idx1] != ';') {
                    if ('0' <= data[end_idx1] && data[end_idx1] <= '9') {
                        if (!termpaintp_input_checked_append_digit(&event.palette_color_report.color_index,
                                                                   10, data[end_idx1] - '0')) {
                            color_index_ok = false;
                        }
                    } else {
                        color_index_ok = false;
                    }
                    end_idx1++;
                }
                if (end_idx1 + 1 < st_offset) {
                    size_t end_idx2 = end_idx1 + 1;
                    while (end_idx2 < st_offset && data[end_idx2] != ';') {
                        end_idx2++;
                    }
                    if (color_index_ok) {
                        event.type = TERMPAINT_EV_PALETTE_COLOR_REPORT;
                        event.palette_color_report.color_desc = (const char*)data + end_idx1 + 1;
                        event.palette_color_report.length = end_idx2 - end_idx1 - 1;
                    }
                } else {
                    event.palette_color_report.color_index = -1;
                    event.type = TERMPAINT_EV_PALETTE_COLOR_REPORT;
                    event.palette_color_report.color_desc = (const char*)data + num_end + 1;
                    event.palette_color_report.length = end_idx1 - num_end - 1;
                }
            }

            if (num_end && ((num >= 10 && num <= 14) || num == 17 || num == 19 || (num >= 705 && num <= 708))) {
                event.type = TERMPAINT_EV_COLOR_SLOT_REPORT;
                event.color_slot_report.slot = num;
                event.color_slot_report.color = (const char*)data + num_end + 1;
                size_t end_idx = num_end + 1;
                while (end_idx < st_offset && data[end_idx] != ';') {
                    end_idx++;
                }
                event.color_slot_report.length = end_idx - num_end - 1;
            }
        }

        if (!event.type && length > 5 && data[0] == '\033' && data[1] == 'P' && data[length-1] == '\\' && data[length-2] == '\033') {
            // DCS sequences
            if (data[2] == '!' && data[3] == '|') {
                event.type = TERMPAINT_EV_RAW_3RD_DEV_ATTRIB;
                event.raw.string = (const char*)data + 4;
                event.raw.length = length - 6;
            }
            if (data[2] == '>' && data[3] == '|') {
                event.type = TERMPAINT_EV_RAW_TERM_NAME;
                event.raw.string = (const char*)data + 4;
                event.raw.length = length - 6;
            }
            if ((data[2] == '1' || data[2] == '0') && data[3] == '+' && data[4] == 'r') {
                event.type = TERMPAINT_EV_RAW_TERMINFO_QUERY_REPLY;
                event.raw.string = (const char*)data + 2;
                event.raw.length = length - 4;
            }
        }
        if (!event.type && length > 3 && data[0] == 0x90 && data[length-1] == 0x9c) {
            // DCS sequences
            if (data[1] == '!' && data[2] == '|') {
                event.type = TERMPAINT_EV_RAW_3RD_DEV_ATTRIB;
                event.raw.string = (const char*)data + 3;
                event.raw.length = length - 4;
            }
        }
    }
    if (!ctx->in_paste) {
        ctx->event_cb(ctx->event_user_data, &event);
    } else {
        // while in paste state ignore anything that is not a plain character.
        // in a paste there shouldn't be any escape sequences, but don't depend on
        // all terminals applying strict filtering.
        if (event.type == TERMPAINT_EV_CHAR && event.c.modifier == 0) {
            termpaint_event event2;
            event2.type = TERMPAINT_EV_PASTE;
            event2.paste.string = event.c.string;
            event2.paste.length = event.c.length;
            event2.paste.initial = false;
            event2.paste.final = false;
            ctx->event_cb(ctx->event_user_data, &event2);
        }
        // some terminals send line breaks as \x0a
        if (event.type == TERMPAINT_EV_CHAR && event.c.modifier == TERMPAINT_MOD_CTRL
                && event.c.length == 1 && event.c.string[0] == 'j') {
            termpaint_event event2;
            event2.type = TERMPAINT_EV_PASTE;
            event2.paste.string = "\n";
            event2.paste.length = 1;
            event2.paste.initial = false;
            event2.paste.final = false;
            ctx->event_cb(ctx->event_user_data, &event2);
        }
        // But some plain strings are handled as keys, so process those as well
        if (event.type == TERMPAINT_EV_KEY && event.key.modifier == 0) {
            termpaint_event event2;
            event2.type = TERMPAINT_EV_PASTE;
            event2.paste.initial = false;
            event2.paste.final = false;
            if (event.key.atom == termpaint_input_space()) {
                event2.paste.string = " ";
                event2.paste.length = 1;
                ctx->event_cb(ctx->event_user_data, &event2);
            }
            if (event.key.atom == termpaint_input_tab()) {
                event2.paste.string = "\t";
                event2.paste.length = 1;
                ctx->event_cb(ctx->event_user_data, &event2);
            }
            if (event.key.atom == termpaint_input_enter()) {
                event2.paste.string = "\r";
                event2.paste.length = 1;
                ctx->event_cb(ctx->event_user_data, &event2);
            }
        }
    }
}

termpaint_input *termpaint_input_new_or_nullptr(void) {
    termpaintp_input_selfcheck();
    termpaint_input *ctx = calloc(1, sizeof(termpaint_input));
    if (!ctx) {
        return nullptr;
    }
    termpaintp_input_reset(ctx);
    ctx->esc_pending = false;
    ctx->raw_filter_cb = nullptr;
    ctx->event_cb = nullptr;
    ctx->expect_cursor_position_report = 0;

    ctx->handle_paste = true;

    return ctx;
}

termpaint_input *termpaint_input_new(void) {
    termpaint_input *ret = termpaint_input_new_or_nullptr();
    if (!ret) {
        abort();
    }
    return ret;
}

void termpaint_input_free(termpaint_input *ctx) {
    if (!ctx) {
        return;
    }

    for (int i = 0; i < ctx->quirks_len; i++) {
        key_mapping_entry* entry = &ctx->quirks[i];
        free((void*)entry->sequence); // cast away const, quirks is always dynamically allocated.
        if (entry->modifiers & MOD_PRINT) {
            free((void*)entry->atom); // cast away const, quirks is always dynamically allocated.
        }
    }
    free(ctx->quirks);
    free(ctx);
}

void termpaint_input_set_raw_filter_cb(termpaint_input *ctx, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data) {
    ctx->raw_filter_cb = cb;
    ctx->raw_filter_user_data = user_data;
}

void termpaint_input_set_event_cb(termpaint_input *ctx, void (*cb)(void *, termpaint_event *), void *user_data) {
    ctx->event_cb = cb;
    ctx->event_user_data = user_data;
}

static bool termpaintp_input_legacy_mouse_bytes_finished(termpaint_input *ctx) {
    const unsigned char cur_ch = ctx->buff[ctx->used - 1];
    if (0xc0 == (0xc0 & cur_ch)) {
        // start of multi code unit sequence.
        if (ctx->used >= 2 && (0xc0 == (0xc0 & ctx->buff[ctx->used - 2]))) {
            // multiple start characters without continuation -> bogus
            return true;
        } else {
            return false;
        }
    } else if (0x80 == (0x80 & cur_ch)) {
        // continuation
        for (int j = ctx->used - 2; j > 0; j--) {
            if (j <= ctx->used - 5 || !(0x80 & ctx->buff[j])) {
                return true;
            }
            if ((ctx->buff[j] & 0xc0) == 0xc0) {
                int len = termpaintp_utf8_len(ctx->buff[j]);
                if (len <= ctx->used - j) {
                    return true;
                } else {
                    return false;
                }
            }
        }
        return true;
    } else {
        return true;
    }
}

void termpaint_input_add_data(termpaint_input *ctx, const char *data_s, unsigned length) {
    const unsigned char *data = (const unsigned char*)data_s;

    for (unsigned i = 0; i < length; i++) {
        // Protect against overlong sequences
        if (ctx->used == MAX_SEQ_LENGTH) {
            // go to error recovery
            ctx->buff[0] = 0;
            ctx->used = 0;
            ctx->overflow = 1;
        }

        const unsigned char cur_ch = data[i];
        ctx->buff[ctx->used] = cur_ch;
        ++ctx->used;
        // Don't use data or i after here!

        bool finished = false;
        bool retrigger = false;
        bool retrigger2 = false; // used in tpis_cmd_str to reprocess "\033[" (last 2 chars)

        switch (ctx->state) {
            case tpis_base:
                // expected: ctx->used == 0

                // detect possible utf-8 multi char start bytes
                if (0xfc == (0xfe & cur_ch)) {
                    ctx->state = tpid_utf8_5;
                } else if (0xf8 == (0xfc & cur_ch)) {
                    ctx->state = tpid_utf8_4;
                } else if (0xf0 == (0xf8 & cur_ch)) {
                    ctx->state = tpid_utf8_3;
                } else if (0xe0 == (0xf0 & cur_ch)) {
                    ctx->state = tpid_utf8_2;
                } else if (0xc0 == (0xe0 & cur_ch)) {
                    ctx->state = tpid_utf8_1;

                // escape sequence starts
                } else if (cur_ch == '\033') {
                    ctx->state = tpis_esc;
                } else if (cur_ch == 0x8f) { // SS3
                    ctx->state = tpis_ss3;
                } else if (cur_ch == 0x90) { // DCS
                    ctx->state = tpis_cmd_str_c1;
                } else if (cur_ch == 0x9b) { // CSI
                    ctx->state = tpis_csi;
                } else if (cur_ch == 0x9d) { // OSC
                    ctx->state = tpis_cmd_str_c1;
                } else {
                    finished = true;
                }
                break;
            case tpis_esc:
                if (cur_ch == 'O') {
                    ctx->state = tpis_ss3;
                } else if (cur_ch == 'P') {
                    ctx->state = tpis_cmd_str;
                } else if (cur_ch == '[') {
                    ctx->state = tpis_csi;
                } else if (cur_ch == ']') {
                    ctx->state = tpis_cmd_str;
                } else if (ctx->expect_apc && cur_ch == '_') { // APC
                    ctx->state = tpis_cmd_str;
                } else if (0xfc == (0xfe & cur_ch)) { // meta -> ESC can produce utf-8 sequences preceeded by an ESC
                    ctx->state = tpid_utf8_5;
                } else if (0xf8 == (0xfc & cur_ch)) {
                    ctx->state = tpid_utf8_4;
                } else if (0xf0 == (0xf8 & cur_ch)) {
                    ctx->state = tpid_utf8_3;
                } else if (0xe0 == (0xf0 & cur_ch)) {
                    ctx->state = tpid_utf8_2;
                } else if (0xc0 == (0xe0 & cur_ch)) {
                    ctx->state = tpid_utf8_1;
                } else if (cur_ch == '\033') {
                    retrigger = true;
                } else {
                    finished = true;
                }
                break;
            case tpis_ss3:
                // this ss3 stuff is totally undocumented. But various codes
                // are seen in the wild that extend these codes by embedding
                // parameters
                if ((cur_ch >= '0' && cur_ch <= '9') || cur_ch == ';') {
                    ;
                } else if (cur_ch == '\033') {
                    retrigger = true;
                } else {
                    finished = true;
                }
                break;
            case tpis_csi:
                if (ctx->used == 3 && cur_ch == 'M' && ctx->buff[ctx->used - 2] == '[' && (ctx->expect_mouse_char_mode || ctx->expect_mouse_multibyte_mode)) {
                    ctx->state = tpis_mouse_btn;
                } else if (cur_ch >= '@' && cur_ch <= '~' && (cur_ch != '[' || ctx->used != 3 /* linux vt*/)) {
                    finished = true;
                } else if (cur_ch == '\033') {
                    retrigger = true;
                }
                break;
            case tpis_cmd_str:
                if (cur_ch == '\033') {
                    ctx->state = tpis_str_terminator_esc;
                } else if (cur_ch == 0x9c || cur_ch == 0x07) {
                    finished = true;
                }
                break;
            case tpis_cmd_str_c1:
                if (cur_ch == 0x9c) {
                    finished = true;
                }
                break;
            case tpis_str_terminator_esc:
                // we expect a '\\' here. But every other char also aborts parsing
                if (cur_ch == '[') {
                    // as a workaround for retriggering:
                    retrigger2 = true;
                } else {
                    finished = true;
                }
                break;
            case tpid_utf8_5:
                if ((cur_ch & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_4;
                }
                break;
            case tpid_utf8_4:
                if ((cur_ch & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_3;
                }
                break;
            case tpid_utf8_3:
                if ((cur_ch & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_2;
                }
                break;
            case tpid_utf8_2:
                if ((cur_ch & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_1;
                }
                break;
            case tpid_utf8_1:
                if ((cur_ch & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    finished = true;
                }
                break;
            case tpis_mouse_btn:
                if (!ctx->expect_mouse_multibyte_mode) {
                    ctx->state = tpis_mouse_col;
                } else {
                    if (termpaintp_input_legacy_mouse_bytes_finished(ctx)) {
                        ctx->state = tpis_mouse_col;
                    }
                }
                break;
            case tpis_mouse_col:
                if (!ctx->expect_mouse_multibyte_mode) {
                    ctx->state = tpis_mouse_row;
                } else {
                    if (termpaintp_input_legacy_mouse_bytes_finished(ctx)) {
                        ctx->state = tpis_mouse_row;
                    }
                }
                break;
            case tpis_mouse_row:
                if (!ctx->expect_mouse_multibyte_mode) {
                    finished = true;
                } else {
                    if (termpaintp_input_legacy_mouse_bytes_finished(ctx)) {
                        finished = true;
                    }
                }
                break;
        }
        if (finished) {
            termpaintp_input_raw(ctx, ctx->buff, ctx->used, ctx->overflow);
            termpaintp_input_reset(ctx);
        } else if (retrigger2) {
            // current and previous char is not part of sequence
            if (ctx->used > 2) {
                termpaintp_input_raw(ctx, ctx->buff, ctx->used - 2, ctx->overflow);
            } else {
                termpaintp_input_raw(ctx, ctx->buff, 0, ctx->overflow);
            }
            termpaintp_input_reset(ctx);
            ctx->buff[ctx->used] = '\033';
            ++ctx->used;
            ctx->buff[ctx->used] = '[';
            ++ctx->used;
            ctx->state = tpis_csi;
        } else if (retrigger) {
            // current char is not part of sequence
            termpaintp_input_raw(ctx, ctx->buff, ctx->used - 1, ctx->overflow);
            termpaintp_input_reset(ctx);
            --i; // process this char again
        }
    }
}



const char *termpaint_input_peek_buffer(const termpaint_input *ctx) {
    return (const char*)ctx->buff;
}


int termpaint_input_peek_buffer_length(const termpaint_input *ctx) {
    return ctx->used;
}

void termpaint_input_expect_cursor_position_report(termpaint_input *ctx) {
    ctx->expect_cursor_position_report += 1;
}

void termpaint_input_expect_legacy_mouse_reports(termpaint_input *ctx, int s) {
    if (s == TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE) {
        ctx->expect_mouse_char_mode = true;
        ctx->expect_mouse_multibyte_mode = false;
    } else if (s == TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE_MODE_1005) {
        ctx->expect_mouse_char_mode = false;
        ctx->expect_mouse_multibyte_mode = true;
    } else {
        ctx->expect_mouse_char_mode = false;
        ctx->expect_mouse_multibyte_mode = false;
    }
}

void termpaint_input_expect_apc_sequences(termpaint_input *ctx, bool enable) {
    ctx->expect_apc = enable;
}

static void termpaintp_input_prepend_quirk(termpaint_input *ctx, const key_mapping_entry *e) {
    // takes ownership of e.sequence;
    key_mapping_entry* new_quirks = calloc(ctx->quirks_len + 1, sizeof(key_mapping_entry));
    if (!new_quirks) {
        abort();
    }
    new_quirks[0] = *e;
    if (ctx->quirks_len) {
        memcpy(new_quirks + 1, ctx->quirks, ctx->quirks_len * sizeof(*ctx->quirks));
    }
    free(ctx->quirks);
    ctx->quirks = new_quirks;
    ctx->quirks_len += 1;
}

void termpaint_input_activate_quirk(termpaint_input *ctx, int quirk) {
    if (quirk == TERMPAINT_INPUT_QUIRK_BACKSPACE_X08_AND_X7F_SWAPPED) {
        {
            key_mapping_entry e;
            e.atom = termpaint_input_backspace();
            e.sequence = strdup("\x08");
            if (!e.sequence) {
                abort();
            }
            e.modifiers = 0;
            termpaintp_input_prepend_quirk(ctx, &e);
        }
        {
            key_mapping_entry e;
            e.atom = termpaint_input_backspace();
            e.sequence = strdup("\x7f");
            if (!e.sequence) {
                abort();
            }
            e.modifiers = TERMPAINT_MOD_CTRL;
            termpaintp_input_prepend_quirk(ctx, &e);
        }
    } else if (quirk == TERMPAINT_INPUT_QUIRK_C1_FOR_CTRL_SHIFT) {
        {
            key_mapping_entry e;
            e.atom = termpaint_input_space();
            e.sequence = strdup("\xc2\x80");
            if (!e.sequence) {
                abort();
            }
            e.modifiers = TERMPAINT_MOD_CTRL | TERMPAINT_MOD_SHIFT;
            termpaintp_input_prepend_quirk(ctx, &e);
        }
        for (int i = 0; i < 26; i++) {
            key_mapping_entry e;
            unsigned char *atom = malloc(2);
            if (!atom) {
                abort();
            }
            unsigned char *sequence = malloc(3);
            if (!sequence) {
                abort();
            }

            atom[0] = 'A' + i;
            atom[1] = 0;

            termpaintp_encode_to_utf8(0x81 + i, sequence);
            sequence[2] = 0;

            e.atom = (char*)atom;
            e.sequence = (char*)sequence;
            e.modifiers = TERMPAINT_MOD_CTRL | TERMPAINT_MOD_SHIFT | MOD_PRINT;
            termpaintp_input_prepend_quirk(ctx, &e);
        }
    }
}

void termpaint_input_handle_paste(termpaint_input *ctx, bool enable) {
    ctx->handle_paste = enable;
    if (!enable) {
        // TODO emit paste end event here too?
        ctx->in_paste = false;
    }
}
