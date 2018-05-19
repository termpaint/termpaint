#include "termpaint_input.h"

#include <string.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h> // for exit

#include "termpaint_compiler.h"

#include "termpaint_utf8.h"

/* Known problems:
 *  * Massivly depends on resync trick. Non resync mode currently no longer supported
 *  * in modOther ctrl-? strange (utf 8 converter?)
 *  * needs to detect utf-8 encoded C1 chars? Or maybe that not used in the wild at all?
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

static key_mapping_entry key_mapping_table[] = {
    { "\x0d", ATOM_enter, 0 }, // also ctrl-m in traditional mode
    { "\e\x0d", ATOM_enter, MOD_ALT },
    XTERM_MODS("\e[27;", ";13~", ATOM_enter), // modifiy other keys mode
    XTERM_MODS("\e[13;", "u", ATOM_enter), // modifiy other keys mode

    { "\x09", ATOM_tab, 0 }, //also ctrl_i
    { "\e\x09", ATOM_tab, MOD_ALT }, //also ctrl-alt-i
    { "\e[Z", ATOM_tab, MOD_SHIFT }, // xterm, normal mode
    XTERM_MODS("\e[27;", ";9~", ATOM_tab), // modifiy other keys mode
    XTERM_MODS("\e[9;", "u", ATOM_tab), // modifiy other keys mode

    { " ", ATOM_space, 0 },
    { "\e ", ATOM_space, MOD_ALT },
    // { "\x00", ATOM_SPACE, MOD_CTRL } via special case in code
    // { "\e\x00", ATOM_space, MOD_CTRL | MOD_ALT} via special case in code
    XTERM_MODS("\e[27;", ";32~", ATOM_space), // modifiy other keys mode
    XTERM_MODS("\e[32;", "u", ATOM_space), // modifiy other keys mode
    //+ also ctrl-2

    { "\e[29~", ATOM_context_menu, 0 },
    // + also shift F4 in linux vt
    XTERM_MODS("\e[29;", "~", ATOM_context_menu),

    { "\e[3~", ATOM_delete, 0 },
    XTERM_MODS("\e[3;", "~", ATOM_delete),
    { "\e[3$", ATOM_delete, MOD_SHIFT},
    { "\e[3^", ATOM_delete, MOD_CTRL},
    { "\e[3@", ATOM_delete, MOD_CTRL | MOD_SHIFT},
    { "\e\e[3~", ATOM_delete, MOD_ALT },
    { "\e\e[3$", ATOM_delete, MOD_ALT | MOD_SHIFT},
    { "\e\e[3^", ATOM_delete, MOD_CTRL | MOD_ALT},
    { "\e\e[3@", ATOM_delete, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\e[3;1~", ATOM_delete, MOD_ALTGR },
    { "\e[F", ATOM_end, 0},
    XTERM_MODS("\e[1;", "F", ATOM_end),
    { "\eOF", ATOM_end,  0},
    { "\e[4~", ATOM_end, 0},
    { "\e[8~", ATOM_end, 0},
    { "\e[8$", ATOM_end, MOD_SHIFT},
    { "\e[8^", ATOM_end, MOD_CTRL},
    { "\e[8@", ATOM_end, MOD_CTRL | MOD_SHIFT},
    { "\e\e[8~", ATOM_end, MOD_ALT},
    { "\e\e[8$", ATOM_end, MOD_ALT | MOD_SHIFT},
    { "\e\e[8^", ATOM_end, MOD_CTRL | MOD_ALT},
    { "\e\e[8@", ATOM_end, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\e[1;1F", ATOM_end, MOD_ALTGR},
    { "\e[H", ATOM_home, 0},
    XTERM_MODS("\e[1;", "H", ATOM_home),
    { "\eOH", ATOM_home,  0},
    { "\e[1~", ATOM_home, 0},
    { "\e[7~", ATOM_home, 0},
    { "\e[7$", ATOM_home, MOD_SHIFT},
    { "\e[7^", ATOM_home, MOD_CTRL},
    { "\e[7@", ATOM_home, MOD_CTRL | MOD_SHIFT},
    { "\e\e[7~", ATOM_home, MOD_ALT},
    { "\e\e[7$", ATOM_home, MOD_ALT | MOD_SHIFT},
    { "\e\e[7^", ATOM_home, MOD_CTRL | MOD_ALT},
    { "\e\e[7@", ATOM_home, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\e[1;1H", ATOM_home, MOD_ALTGR},
    { "\e[2~", ATOM_insert, 0},
    XTERM_MODS("\e[2;", "~", ATOM_insert),
    { "\e[2$", ATOM_insert, MOD_SHIFT},
    { "\e[2^", ATOM_insert, MOD_CTRL},
    { "\e[2@", ATOM_insert, MOD_CTRL | MOD_SHIFT},
    { "\e\e[2~", ATOM_insert, MOD_ALT},
    { "\e\e[2$", ATOM_insert, MOD_ALT | MOD_SHIFT},
    { "\e\e[2^", ATOM_insert, MOD_CTRL | MOD_ALT},
    { "\e\e[2@", ATOM_insert, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\e[2;1~", ATOM_insert, MOD_ALTGR},
    { "\e[6~", ATOM_page_down, 0},
    XTERM_MODS("\e[6;", "~", ATOM_page_down),
    { "\e[6$", ATOM_page_down, MOD_SHIFT},
    { "\e[6^", ATOM_page_down, MOD_CTRL},
    { "\e[6@", ATOM_page_down, MOD_CTRL | MOD_SHIFT},
    { "\e\e[6~", ATOM_page_down, MOD_ALT},
    { "\e\e[6$", ATOM_page_down, MOD_ALT | MOD_SHIFT},
    { "\e\e[6^", ATOM_page_down, MOD_CTRL | MOD_ALT},
    { "\e\e[6@", ATOM_page_down, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\e[6;1~", ATOM_page_down, MOD_ALTGR},
    { "\e[5~", ATOM_page_up, 0},
    XTERM_MODS("\e[5;", "~", ATOM_page_up), // shift combinations only available when scroll bindings are removed in xterm
    { "\e[5$", ATOM_page_up, MOD_SHIFT},
    { "\e[5^", ATOM_page_up, MOD_CTRL},
    { "\e[5@", ATOM_page_up, MOD_CTRL | MOD_SHIFT},
    { "\e\e[5~", ATOM_page_up, MOD_ALT},
    { "\e\e[5$", ATOM_page_up, MOD_ALT | MOD_SHIFT},
    { "\e\e[5^", ATOM_page_up, MOD_CTRL | MOD_ALT},
    { "\e\e[5@", ATOM_page_up, MOD_CTRL | MOD_ALT | MOD_SHIFT},
    { "\e[5;1~", ATOM_page_up, MOD_ALTGR},

    { "\e[B", ATOM_arrow_down, 0 },
    XTERM_MODS("\e[1;", "B", ATOM_arrow_down),
    { "\eOB", ATOM_arrow_down, 0 },
    { "\e[b", ATOM_arrow_down, MOD_SHIFT },
    { "\eOb", ATOM_arrow_down, MOD_CTRL },
    { "\e\e[B", ATOM_arrow_down, MOD_ALT },
    { "\e\e[b", ATOM_arrow_down, MOD_ALT | MOD_SHIFT },
    { "\e\eOb", ATOM_arrow_down, MOD_CTRL | MOD_ALT },
    { "\e[1;1B", ATOM_arrow_down, MOD_ALTGR },
    { "\e[D", ATOM_arrow_left, 0 },
    XTERM_MODS("\e[1;", "D", ATOM_arrow_left),
    { "\eOD", ATOM_arrow_left, 0 },
    { "\e[d", ATOM_arrow_left, MOD_SHIFT },
    { "\eOd", ATOM_arrow_left, MOD_CTRL },
    { "\e\e[D", ATOM_arrow_left, MOD_ALT },
    { "\e\e[d", ATOM_arrow_left, MOD_ALT | MOD_SHIFT },
    { "\e\eOd", ATOM_arrow_left, MOD_CTRL | MOD_ALT },
    { "\e[1;1D", ATOM_arrow_left, MOD_ALTGR },
    { "\e[C", ATOM_arrow_right, 0 },
    XTERM_MODS("\e[1;", "C", ATOM_arrow_right),
    { "\eOC", ATOM_arrow_right, 0 },
    { "\e[c", ATOM_arrow_right, MOD_SHIFT },
    { "\eOc", ATOM_arrow_right, MOD_CTRL },
    { "\e\e[C", ATOM_arrow_right, MOD_ALT },
    { "\e\e[c", ATOM_arrow_right, MOD_ALT | MOD_SHIFT },
    { "\e\eOc", ATOM_arrow_right, MOD_CTRL | MOD_ALT },
    { "\e[1;1C", ATOM_arrow_right, MOD_ALTGR },
    { "\e[A", ATOM_arrow_up, 0 },
    XTERM_MODS("\e[1;", "A", ATOM_arrow_up),
    { "\eOA", ATOM_arrow_up, 0 },
    { "\e[a", ATOM_arrow_up, MOD_SHIFT },
    { "\eOa", ATOM_arrow_up, MOD_CTRL },
    { "\e\e[A", ATOM_arrow_up, MOD_ALT },
    { "\e\e[a", ATOM_arrow_up, MOD_ALT | MOD_SHIFT },
    { "\e\eOa", ATOM_arrow_up, MOD_CTRL | MOD_ALT },
    { "\e[1;1A", ATOM_arrow_up, MOD_ALTGR },

    { "\eOo", ATOM_numpad_divide, 0 },
    { "\e\eOo", ATOM_numpad_divide, MOD_ALT },
    XTERM_MODS("\eO", "o", ATOM_numpad_divide), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOj", ATOM_numpad_multiply, 0 },
    { "\e\eOj", ATOM_numpad_multiply, MOD_ALT },
    XTERM_MODS("\eO", "j", ATOM_numpad_multiply), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOm", ATOM_numpad_subtract, 0 },
    { "\e\eOm", ATOM_numpad_subtract, MOD_ALT },
    XTERM_MODS("\eO", "m", ATOM_numpad_subtract), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOk", ATOM_numpad_add, 0 },
    { "\e\eOk", ATOM_numpad_add, MOD_ALT },
    XTERM_MODS("\eO", "k", ATOM_numpad_add), // ctrl-alt (not shifted) not reachable in xterm
    { "\eOM", ATOM_numpad_enter, 0 },
    { "\e\eOM", ATOM_numpad_enter, MOD_ALT },
    XTERM_MODS("\eO", "M", ATOM_numpad_enter),
    //{ "\e[3~", ATOM_numpad_decimal, 0 },
    //XTERM_MODS("\e[3;", "~", ATOM_numpad_decimal), // shifted combinations produce other codes in xterm
    { "\eO2l", ATOM_numpad_decimal,                      MOD_SHIFT },
    { "\eO6l", ATOM_numpad_decimal, MOD_CTRL           | MOD_SHIFT },
    { "\eO4l", ATOM_numpad_decimal,            MOD_ALT | MOD_SHIFT },
    { "\eO8l", ATOM_numpad_decimal, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOl",  ATOM_numpad_decimal, MOD_CTRL                       },
    { "\eOn",  ATOM_numpad_decimal,                              0 },
    { "\e\eOn",ATOM_numpad_decimal,            MOD_ALT             },

    { "\eO2p", ATOM_numpad0,                             MOD_SHIFT },
    { "\eO6p", ATOM_numpad0,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4p", ATOM_numpad0,                   MOD_ALT | MOD_SHIFT },
    { "\eO8p", ATOM_numpad0,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOp",  ATOM_numpad0,                                     0 },
    { "\e\eOp",ATOM_numpad0,                   MOD_ALT             },


    { "\eO2q", ATOM_numpad1,                             MOD_SHIFT },
    { "\eO6q", ATOM_numpad1,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4q", ATOM_numpad1,                   MOD_ALT | MOD_SHIFT },
    { "\eO8q", ATOM_numpad1,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOq",  ATOM_numpad1,                                     0 },
    { "\e\eOq",ATOM_numpad1,                   MOD_ALT             },

    { "\eO2r", ATOM_numpad2,                             MOD_SHIFT },
    { "\eO6r", ATOM_numpad2,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4r", ATOM_numpad2,                   MOD_ALT | MOD_SHIFT },
    { "\eO8r", ATOM_numpad2,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOr",  ATOM_numpad2,                                     0 },
    { "\e\eOr",ATOM_numpad2,                   MOD_ALT             },

    { "\eO2s", ATOM_numpad3,                             MOD_SHIFT },
    { "\eO6s", ATOM_numpad3,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4s", ATOM_numpad3,                   MOD_ALT | MOD_SHIFT },
    { "\eO8s", ATOM_numpad3,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOs",  ATOM_numpad3,                                     0 },
    { "\e\eOs",ATOM_numpad3,                   MOD_ALT             },

    { "\eO2t", ATOM_numpad4,                             MOD_SHIFT },
    { "\eO6t", ATOM_numpad4,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4t", ATOM_numpad4,                   MOD_ALT | MOD_SHIFT },
    { "\eO8t", ATOM_numpad4,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOt",  ATOM_numpad4,                                     0 },
    { "\e\eOt",ATOM_numpad4,                   MOD_ALT             },

    { "\eO2u", ATOM_numpad5,                             MOD_SHIFT },
    { "\eO6u", ATOM_numpad5,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4u", ATOM_numpad5,                   MOD_ALT | MOD_SHIFT },
    { "\eO8u", ATOM_numpad5,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOu",  ATOM_numpad5,                                     0 },
    { "\e\eOu",ATOM_numpad5,                   MOD_ALT             },
    { "\e[E", ATOM_numpad5, 0 },
    XTERM_MODS("\e[1;", "E", ATOM_numpad5),
    { "\eOE", ATOM_numpad5, 0 },
    { "\e[G",  ATOM_numpad5,                                     0 },

    { "\eO2v", ATOM_numpad6,                             MOD_SHIFT },
    { "\eO6v", ATOM_numpad6,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4v", ATOM_numpad6,                   MOD_ALT | MOD_SHIFT },
    { "\eO8v", ATOM_numpad6,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOv",  ATOM_numpad6,                                     0 },
    { "\e\eOv",ATOM_numpad6,                   MOD_ALT             },

    { "\eO2w", ATOM_numpad7,                             MOD_SHIFT },
    { "\eO6w", ATOM_numpad7,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4w", ATOM_numpad7,                   MOD_ALT | MOD_SHIFT },
    { "\eO8w", ATOM_numpad7,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOw",  ATOM_numpad7,                                     0 },
    { "\e\eOw",ATOM_numpad7,                   MOD_ALT             },

    { "\eO2x", ATOM_numpad8,                             MOD_SHIFT },
    { "\eO6x", ATOM_numpad8,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4x", ATOM_numpad8,                   MOD_ALT | MOD_SHIFT },
    { "\eO8x", ATOM_numpad8,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOx",  ATOM_numpad8,                                     0 },
    { "\e\eOx",ATOM_numpad8,                   MOD_ALT             },

    { "\eO2y", ATOM_numpad9,                             MOD_SHIFT },
    { "\eO6y", ATOM_numpad9,        MOD_CTRL           | MOD_SHIFT },
    { "\eO4y", ATOM_numpad9,                   MOD_ALT | MOD_SHIFT },
    { "\eO8y", ATOM_numpad9,        MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eOy",  ATOM_numpad9,                                     0 },
    { "\e\eOy",ATOM_numpad9,                   MOD_ALT             },

    // { "\e", ATOM_escape, }, via special case in code (also Ctrl-[ in traditional mode)
    XTERM_MODS("\e[27;", ";27~", ATOM_escape), // modifiy other keys mode
    XTERM_MODS("\e[27;", "u", ATOM_escape), // modifiy other keys mode
    { "\e\e", ATOM_escape,                                 MOD_ALT },


    { "\eOP", ATOM_f1, 0 },
    XTERM_MODS("\e[1;", "P", ATOM_f1),
    XTERM_MODS("\eO", "P", ATOM_f1),
    { "\e[[A", ATOM_f1, 0 },
    { "\e[25~", ATOM_f1, MOD_SHIFT },
    { "\e[25^", ATOM_f1, MOD_CTRL | MOD_SHIFT },
    { "\e\e[25~", ATOM_f1, MOD_ALT | MOD_SHIFT },
    { "\e\e[25^", ATOM_f1, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eO1P", ATOM_f1, MOD_ALTGR },
    { "\e[11~", ATOM_f1, 0 },
    { "\e[11^", ATOM_f1, MOD_CTRL },
    { "\e\e[11~", ATOM_f1, MOD_ALT },
    { "\e\e[11^", ATOM_f1, MOD_CTRL | MOD_ALT },
    { "\eOQ", ATOM_f2, 0 },
    XTERM_MODS("\e[1;", "Q", ATOM_f2),
    XTERM_MODS("\eO", "Q", ATOM_f2),
    { "\e[[B", ATOM_f2, 0 },
    { "\e[26~", ATOM_f2, MOD_SHIFT },
    { "\e[26^", ATOM_f2, MOD_CTRL | MOD_SHIFT },
    { "\e\e[26~", ATOM_f2, MOD_ALT | MOD_SHIFT },
    { "\e\e[26^", ATOM_f2, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eO1Q", ATOM_f2, MOD_ALTGR },
    { "\e[12~", ATOM_f2, 0 },
    { "\e[12^", ATOM_f2, MOD_CTRL },
    { "\e\e[12~", ATOM_f2, MOD_ALT },
    { "\e\e[12^", ATOM_f2, MOD_CTRL | MOD_ALT },
    { "\eOR", ATOM_f3, 0 },
    XTERM_MODS("\e[1;", "R", ATOM_f3),
    XTERM_MODS("\eO", "R", ATOM_f3),
    { "\e[[C", ATOM_f3, 0 },
    { "\e[28~", ATOM_f3, MOD_SHIFT },
    { "\e[28^", ATOM_f3, MOD_CTRL | MOD_SHIFT },
    { "\e\e[28~", ATOM_f3, MOD_ALT | MOD_SHIFT },
    { "\e\e[28^", ATOM_f3, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\eO1R", ATOM_f3, MOD_ALTGR },
    { "\e[13~", ATOM_f3, 0 },
    { "\e[13^", ATOM_f3, MOD_CTRL },
    { "\e\e[13~", ATOM_f3, MOD_ALT },
    { "\e\e[13^", ATOM_f3, MOD_CTRL | MOD_ALT },
    { "\eOS", ATOM_f4, 0 },
    XTERM_MODS("\e[1;", "S", ATOM_f4),    
    XTERM_MODS("\eO", "S", ATOM_f4),
    { "\e[[D", ATOM_f4, 0 },
    { "\eO1S", ATOM_f4, MOD_ALTGR },
    { "\e[14~", ATOM_f4, 0 },
    { "\e[14^", ATOM_f4, MOD_CTRL },
    { "\e\e[14~", ATOM_f4, MOD_ALT },
    { "\e\e[14^", ATOM_f4, MOD_CTRL | MOD_ALT },
    { "\e[29^", ATOM_f4, MOD_CTRL | MOD_SHIFT },
    { "\e\e[29~", ATOM_f4, MOD_ALT | MOD_SHIFT },
    { "\e\e[29^", ATOM_f4, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\e[15~", ATOM_f5, 0 },
    { "\e[15^", ATOM_f5, MOD_CTRL },
    { "\e\e[15~", ATOM_f5, MOD_ALT },
    { "\e\e[15^", ATOM_f5, MOD_CTRL | MOD_ALT },
    XTERM_MODS("\e[15;", "~", ATOM_f5),
    { "\e[[E", ATOM_f5, 0 },
    { "\e[31~", ATOM_f5, MOD_SHIFT },
    { "\e[31^", ATOM_f5, MOD_CTRL | MOD_SHIFT },
    { "\e\e[31~", ATOM_f5, MOD_ALT | MOD_SHIFT },
    { "\e\e[31^", ATOM_f5, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\e[15;1~", ATOM_f5, MOD_ALTGR },
    { "\e[17~", ATOM_f6, 0 },
    { "\e[17^", ATOM_f6, MOD_CTRL },
    { "\e\e[17~", ATOM_f6, MOD_ALT },
    { "\e\e[17^", ATOM_f6, MOD_CTRL | MOD_ALT },
    XTERM_MODS("\e[17;", "~", ATOM_f6),
    { "\e[32~", ATOM_f6, MOD_SHIFT },
    { "\e[32^", ATOM_f6, MOD_CTRL | MOD_SHIFT },
    { "\e\e[32~", ATOM_f6, MOD_ALT | MOD_SHIFT },
    { "\e\e[32^", ATOM_f6, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\e[17;1~", ATOM_f6, MOD_ALTGR },
    { "\e[18~", ATOM_f7, 0 },
    { "\e[18^", ATOM_f7, MOD_CTRL },
    { "\e\e[18~", ATOM_f7, MOD_ALT },
    { "\e\e[18^", ATOM_f7, MOD_CTRL | MOD_ALT },
    XTERM_MODS("\e[18;", "~", ATOM_f7),
    { "\e[33~", ATOM_f7, MOD_SHIFT },
    { "\e[33^", ATOM_f7, MOD_CTRL | MOD_SHIFT },
    { "\e\e[33~", ATOM_f7, MOD_ALT | MOD_SHIFT },
    { "\e\e[33^", ATOM_f7, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\e[18;1~", ATOM_f7, MOD_ALTGR },
    { "\e[19~", ATOM_f8, 0 },
    { "\e[19^", ATOM_f8, MOD_CTRL },
    { "\e\e[19~", ATOM_f8, MOD_ALT },
    { "\e\e[19^", ATOM_f8, MOD_CTRL | MOD_ALT },
    XTERM_MODS("\e[19;", "~", ATOM_f8),
    { "\e[34~", ATOM_f8, MOD_SHIFT },
    { "\e[34^", ATOM_f8, MOD_CTRL | MOD_SHIFT },
    { "\e\e[34~", ATOM_f8, MOD_ALT | MOD_SHIFT },
    { "\e\e[34^", ATOM_f8, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    { "\e[19;1~", ATOM_f8, MOD_ALTGR },
    { "\e[20~", ATOM_f9, 0 },
    { "\e[20^", ATOM_f9, MOD_CTRL },
    { "\e\e[20~", ATOM_f9, MOD_ALT },
    { "\e\e[20^", ATOM_f9, MOD_CTRL | MOD_ALT },
    XTERM_MODS("\e[20;", "~", ATOM_f9),
    { "\e[20;1~", ATOM_f9, MOD_ALTGR },
    { "\e[21~", ATOM_f10, 0 },
    { "\e[21^", ATOM_f10, MOD_CTRL },
    { "\e\e[21~", ATOM_f10, MOD_ALT },
    { "\e\e[21^", ATOM_f10, MOD_CTRL | MOD_ALT },
    XTERM_MODS("\e[21;", "~", ATOM_f10),
    { "\e[21;1~", ATOM_f10, MOD_ALTGR },
    { "\e[23~", ATOM_f11, 0 },
    { "\e[23$", ATOM_f11, MOD_SHIFT },
    { "\e[23^", ATOM_f11, MOD_CTRL },
    { "\e[23@", ATOM_f11, MOD_CTRL | MOD_SHIFT },
    { "\e\e[23~", ATOM_f11, MOD_ALT },
    { "\e\e[23$", ATOM_f11, MOD_ALT | MOD_SHIFT },
    { "\e\e[23^", ATOM_f11, MOD_CTRL | MOD_ALT },
    { "\e\e[23@", ATOM_f11, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    XTERM_MODS("\e[23;", "~", ATOM_f11),
    { "\e[23;1~", ATOM_f11, MOD_ALTGR },
    { "\e[24~", ATOM_f12, 0 },
    { "\e[24$", ATOM_f12, MOD_SHIFT },
    { "\e[24^", ATOM_f12, MOD_CTRL },
    { "\e[24@", ATOM_f12, MOD_CTRL | MOD_SHIFT },
    { "\e\e[24~", ATOM_f12, MOD_ALT },
    { "\e\e[24$", ATOM_f12, MOD_ALT | MOD_SHIFT },
    { "\e\e[24^", ATOM_f12, MOD_CTRL | MOD_ALT },
    { "\e\e[24@", ATOM_f12, MOD_CTRL | MOD_ALT | MOD_SHIFT },
    XTERM_MODS("\e[24;", "~", ATOM_f12),
    { "\e[24;1~", ATOM_f12, MOD_ALTGR },


    //{ "", ATOM_, 0 },
    //{ "", ATOM_, 0 },
    //{ "", ATOM_, 0 },

    { "\x01",   "a", MOD_CTRL |           MOD_PRINT },
    { "\e\x01", "a", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x02",   "b", MOD_CTRL |           MOD_PRINT },
    { "\e\x02", "b", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x03",   "c", MOD_CTRL |           MOD_PRINT },
    { "\e\x03", "c", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x04",   "d", MOD_CTRL |           MOD_PRINT },
    { "\e\x04", "d", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x05",   "e", MOD_CTRL |           MOD_PRINT },
    { "\e\x05", "e", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x06",   "f", MOD_CTRL |           MOD_PRINT },
    { "\e\x06", "f", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x07",   "g", MOD_CTRL |           MOD_PRINT },
    { "\e\x07", "g", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //{ "\x08",   "h", MOD_CTRL |           MOD_PRINT },
    //+ also ctrl-Backspace
    //{ "\e\x08", "h", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also ctrl-alt-Backspace (which might not be usable as xorg binds zap to it)
    //{ "\x09",   "i",                      MOD_PRINT },
    //+ also Tab, Ctrl-Tab
    //{ "\e\x09", "i",            MOD_ALT | MOD_PRINT },
    //+ also Alt-Tab
    { "\x0a",   "j", MOD_CTRL |           MOD_PRINT },
    { "\e\x0a", "j", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x0b",   "k", MOD_CTRL |           MOD_PRINT },
    { "\e\x0b", "k", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x0c",   "l", MOD_CTRL |           MOD_PRINT },
    { "\e\x0c", "l", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //{ "\x0d",   "m", MOD_CTRL |           MOD_PRINT },
    //+ also Return, Ctrl-Return
    //{ "\e\x0d", "m", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also alt-Return, alt-Ctrl-Return
    { "\x0e",   "n", MOD_CTRL |           MOD_PRINT },
    { "\e\x0e", "n", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x0f",   "o", MOD_CTRL |           MOD_PRINT },
    { "\e\x0f", "o", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x10",   "p", MOD_CTRL |           MOD_PRINT },
    { "\e\x10", "p", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x11",   "q", MOD_CTRL |           MOD_PRINT },
    { "\e\x11", "q", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x12",   "r", MOD_CTRL |           MOD_PRINT },
    { "\e\x12", "r", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x13",   "s", MOD_CTRL |           MOD_PRINT },
    { "\e\x13", "s", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x14",   "t", MOD_CTRL |           MOD_PRINT },
    { "\e\x14", "t", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x15",   "u", MOD_CTRL |           MOD_PRINT },
    { "\e\x15", "u", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x16",   "v", MOD_CTRL |           MOD_PRINT },
    { "\e\x16", "v", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x17",   "w", MOD_CTRL |           MOD_PRINT },
    { "\e\x17", "w", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x18",   "x", MOD_CTRL |           MOD_PRINT },
    { "\e\x18", "x", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x19",   "y", MOD_CTRL |           MOD_PRINT },
    { "\e\x19", "y", MOD_CTRL | MOD_ALT | MOD_PRINT },
    { "\x1a",   "z", MOD_CTRL |           MOD_PRINT },
    { "\e\x1a", "z", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //{ "\x1b",   "[", MOD_CTRL |           MOD_PRINT },
    //+ also ESC
    //+ also ctrl-3
    { "\x1c",   "\\", MOD_CTRL |           MOD_PRINT },
    { "\e\x1c", "\\", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also ctrl-4
    { "\x1d",   "]", MOD_CTRL |           MOD_PRINT },
    //+ also ctrl-5
    { "\e\x1d", "]", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also alt-ctrl-5
    { "\x1e",   "~", MOD_CTRL |           MOD_PRINT },
    //+ also ctrl-6
    { "\e\x1e", "~", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also alt-ctrl-6
    { "\x1f",   "?", MOD_CTRL |           MOD_PRINT },
    { "\e\x1f", "?", MOD_CTRL | MOD_ALT | MOD_PRINT },
    //+ also ctrl-7
    { "\x7f", ATOM_backspace, 0 },
    { "\x08", ATOM_backspace, MOD_CTRL },
    { "\e\x08", ATOM_backspace, MOD_CTRL | MOD_ALT },
    { "\e\x7f", ATOM_backspace, MOD_ALT },
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

void termpaintp_input_dump_table() {
    FILE * f = fopen("input.dump", "w");
    for (key_mapping_entry* entry_a = key_mapping_table; entry_a->sequence != nullptr; entry_a++) {
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
    tpis_str_terminator_esc,
    tpid_utf8_5, tpid_utf8_4, tpid_utf8_3, tpid_utf8_2, tpid_utf8_1
};

struct termpaint_input_ {
    unsigned char buff[MAX_SEQ_LENGTH];
    int used;
    enum termpaint_input_state state;
    _Bool overflow;
    _Bool esc_pending;

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

static bool termpaintp_input_parse_dec_2(const unsigned char *data, size_t length, int *a, int *b) {
    int val = 0;
    int state = 0;
    for (int i = 0; i < length; i++) {
        if (data[i] >= '0' && data[i] <= '9') {
            val = val * 10 + data[i]-'0';
        } else if (state == 0 && data[i] == ';') {
            *a = val;
            val = 0;
            state = 1;
        } else if (state == 1 && data[i] == ';'){
            *b = val;
            return true;
        } else {
            return false;
        }
    }
    if (state == 1) {
        *b = val;
        return true;
    }

    return false;
}

static void termpaintp_input_raw(termpaint_input *ctx, const unsigned char *data, size_t length, _Bool overflow) {
    unsigned char dbl_esc_tmp[21];
    // First handle double escape for alt-ESC
    if (overflow) {
        // overflow just reset to base state.
        ctx->esc_pending = false;
    } else {
        if (!ctx->esc_pending) {
            if (length == 1 && data[0] == '\e') {
                // skip processing this, either next key or resync will trigger real handling
                ctx->esc_pending = true;
                return;
            }
        } else {
            ctx->esc_pending = false;

            bool found = false;

            if (length + 1 < sizeof (dbl_esc_tmp)) {
                dbl_esc_tmp[0] = '\e';
                memcpy(dbl_esc_tmp + 1, data, length);
                for (key_mapping_entry* entry = key_mapping_table; entry->sequence != nullptr; entry++) {
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
                if (ctx->raw_filter_cb && ctx->raw_filter_cb(ctx->raw_filter_user_data, (const char *)"\e", 1, false)) {
                    ; // skipped by raw filter
                } else if (ctx->event_cb) {
                    termpaint_event event;
                    event.type = TERMPAINT_EV_KEY;
                    event.key.length = 0;
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
    } else if (length == 1 && data[0] == 0) {
        event.type = TERMPAINT_EV_KEY;
        event.key.length = 0;
        event.key.atom = ATOM_space;
        event.key.modifier = MOD_CTRL;
    } else if (length == 2 && data[0] == '\e' && data[1] == 0) {
        event.type = TERMPAINT_EV_KEY;
        event.key.length = 0;
        event.key.atom = ATOM_space;
        event.key.modifier = MOD_CTRL | MOD_ALT;
    } else {
        // TODO optimize
        for (key_mapping_entry* entry = key_mapping_table; entry->sequence != nullptr; entry++) {
            if (strlen(entry->sequence) == length && memcmp(entry->sequence, data, length) == 0) {
                if (entry->modifiers & MOD_PRINT) {
                    // special case for ctrl-X which is in the table but a modified printable
                    event.type = TERMPAINT_EV_CHAR;
                    event.c.length = strlen(entry->atom);
                    event.c.string = entry->atom;
                    event.c.modifier = entry->modifiers & ~MOD_PRINT;
                    break;
                } else {
                    event.type = TERMPAINT_EV_KEY;
                    event.key.length = 0;
                    event.key.atom = entry->atom;
                    event.key.modifier = entry->modifiers;
                    break;
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
        if (!event.type && length >= 2 && data[0] == '\e' && (0xc0 == (0xc0 & data[1]))) {
            // bogus: tokenizer should ensure that this is exactly one valid utf8 codepoint
            event.type = termpaintp_check_valid_sequence(data+1, length - 1) ? TERMPAINT_EV_CHAR : TERMPAINT_EV_INVALID_UTF8;
            event.c.length = length-1;
            event.c.string = (const char*)data+1;
            event.c.modifier = MOD_ALT;
        }
        if (!event.type && length == 2 && data[0] == '\e' && data[1] > 32 && data[1] < 127) {
            event.type = TERMPAINT_EV_CHAR;
            event.c.length = length-1;
            event.c.string = (const char*)data+1;
            event.c.modifier = MOD_ALT;
        }
        if (!event.type && length >= 1 && (0xc0 == (0xc0 & data[0]))) {
            // tokenizer should ensure that this is exactly one valid utf8 codepoint
            event.type = termpaintp_check_valid_sequence(data, length) ? TERMPAINT_EV_CHAR : TERMPAINT_EV_INVALID_UTF8;
            event.c.length = length;
            event.c.string = (const char*)data;
            event.c.modifier = 0;
        }
        if (!event.type && length > 0 && data[0] > 32 && data[0] < 127) {
            event.type = TERMPAINT_EV_CHAR;
            event.c.length = length;
            event.c.string = (const char*)data;
            event.c.modifier = 0;
        }

        if (!event.type && length > 2 && data[0] == '\033' && data[1] == '[') {
            int i = 2;
            bool qm = false;
            if (length > 3 && data[i] == '?') {
                ++i;
                qm = true;
            }
            if (length > 5 && data[length-1] == 'R') { // both plain and qm
                int x, y;
                if (termpaintp_input_parse_dec_2(data + i, length - i - 1, &y, &x)) {
                    event.type = TERMPAINT_EV_CURSOR_POSITION;
                    event.cursor_position.x = x - 1;
                    event.cursor_position.y = y - 1;
                }
            }

            if (length > 5 && data[length-1] == 'y' && data[length-2] == '$') { // both plain and qm
                int mode, status;
                if (termpaintp_input_parse_dec_2(data + i, length - i - 2, &mode, &status)) {
                    event.type = TERMPAINT_EV_MODE_REPORT;
                    event.mode.number = mode;
                    event.mode.kind = qm ? 1 : 0;
                    event.mode.status = status;
                }
            }

            if (length > 3 && data[2] == '>' && data[length-1] == 'c') {
                event.type = TERMPAINT_EV_RAW_SEC_DEV_ATTRIB;
                event.raw.string = (const char*)data;
                event.raw.length = length;
            }

            if (length > 2 && data[length-1] == 'x') {
                event.type = TERMPAINT_EV_RAW_DECREQTPARM;
                event.raw.string = (const char*)data;
                event.raw.length = length;
            }
        }
    }
    ctx->event_cb(ctx->event_user_data, &event);
}

termpaint_input *termpaint_input_new() {
    termpaintp_input_selfcheck();
    termpaint_input *ctx = calloc(1, sizeof(termpaint_input));
    termpaintp_input_reset(ctx);
    ctx->esc_pending = false;
    ctx->raw_filter_cb = nullptr;
    ctx->event_cb = nullptr;
    return ctx;
}

void termpaint_input_free(termpaint_input *ctx) {
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

bool termpaint_input_add_data(termpaint_input *ctx, const char *data_s, unsigned length) {
    const unsigned char *data = (const unsigned char*)data_s;

    // TODO utf8
    for (unsigned i = 0; i < length; i++) {
        // Protect against overlong sequences
        if (ctx->used == MAX_SEQ_LENGTH) {
            // go to error recovery
            ctx->buff[0] = 0;
            ctx->used = 0;
            ctx->overflow = 1;
        }

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
                } else if (data[i] == 0x8f) { // SS3
                    ctx->state = tpis_ss3;
                } else if (data[i] == 0x90) { // DCS
                    ctx->state = tpis_cmd_str;
                } else if (data[i] == 0x9b) { // CSI
                    ctx->state = tpis_csi;
                } else if (data[i] == 0x9d) { // OSC
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
                // parameters
                if ((data[i] >= '0' && data[i] <= '9') || data[i] == ';') {
                    ;
                } else if (data[i] == '\e') {
                    retrigger = true;
                } else {
                    finished = true;
                }
                break;
            case tpis_csi:
                if (data[i] >= '@' && data[i] <= '~' && (data[i] != '[' || ctx->used != 3 /* linux vt*/)) {
                    finished = true;
                } else if (data[i] == '\e') {
                    retrigger = true;
                }
                break;
            case tpis_cmd_str:
                if (data[i] == '\e') {
                    ctx->state = tpis_str_terminator_esc;
                } else if (data[i] == 0x9c) {
                    finished = true;
                }
                break;
            case tpis_str_terminator_esc:
                // we expect a '\\' here. But every other char also aborts parsing
                if (data[i] == '[') {
                    // as a workaround for retriggering:
                    retrigger2 = true;
                } else {
                    finished = true;
                }
                break;
            case tpid_utf8_5:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_4;
                }
                break;
            case tpid_utf8_4:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_3;
                }
                break;
            case tpid_utf8_3:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_2;
                }
                break;
            case tpid_utf8_2:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    ctx->state = tpid_utf8_1;
                }
                break;
            case tpid_utf8_1:
                if ((data[i] & 0xc0) != 0x80) {
                    // encoding error, abort sequence
                    retrigger = true;
                } else {
                    finished = true;
                }
                break;
        }
        if (finished) {
            termpaintp_input_raw(ctx, ctx->buff, ctx->used, ctx->overflow);
            termpaintp_input_reset(ctx);
        } else if (retrigger2) {
            // current and previous char is not part of sequence
            if (ctx->used >= 2) {
                termpaintp_input_raw(ctx, ctx->buff, ctx->used - 2, ctx->overflow);
            } else {
                termpaintp_input_raw(ctx, ctx->buff, 0, ctx->overflow);
            }
            termpaintp_input_reset(ctx);
            ctx->buff[ctx->used] = '\e';
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
    return false;
}



const char *termpaint_input_peek_buffer(termpaint_input *ctx) {
    return (const char*)ctx->buff;
}


int termpaint_input_peek_buffer_length(termpaint_input *ctx) {
    return ctx->used;
}
