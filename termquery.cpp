// SPDX-License-Identifier: BSL-1.0
#include <unistd.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include <vector>
#include <map>
#include <string>
#include <functional>

typedef bool _Bool;

#include "termpaint.h"
#include "termpaintx.h"
#include "termpaint_input.h"

std::vector<std::string> ring;
std::vector<std::string> ring2;
std::string peek_buffer;
termpaint_surface *surface;

struct info {
    std::string name;
    std::string query;
    std::string user;
    std::function<std::string(info &i)> decoder;
    std::string raw;
    std::string pretty;
};

std::vector<info> info_vec;
info *current_query = nullptr;
bool finished = false;

void kick() {
    for (auto &i : info_vec) {
        if (i.raw.size() == 0) {
            printf("%s", i.query.data());
            fflush(stdout);
            current_query = &i;
            return;
        }
    }
    finished = true;
}

_Bool raw_filter(void *user_data, const char *data, unsigned length, _Bool overflow) {
    (void)user_data; (void) overflow;
    std::string event { data, length };
    info &i = *current_query;
    i.raw = event;
    i.pretty = i.decoder(i);

    puts((i.name + ": " + i.pretty.data()).data());

    kick();
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_input *input = termpaint_input_new();
    termpaint_input_set_raw_filter_cb(input, raw_filter, 0);

    struct termios tattr;
    struct termios otattr;

    tcgetattr (STDIN_FILENO, &otattr);
    tcgetattr (STDIN_FILENO, &tattr);
    tattr.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);

    auto addMode = [] (std::string mode, std::string name) {
        info i;
        i.name = name;
        i.query = "\033[" + mode + "$p";
        i.user = "\033[" + mode + ";";
        i.decoder = [] (info &i) {
            if (i.raw.find_first_of(i.user) == 0) {
                char result = i.raw[i.user.size()];
                if (result == '0') {
                    return "not recognized";
                } else if (result == '1') {
                    return "set";
                } else if (result == '2') {
                    return "reset";
                } else if (result == '3') {
                    return "perm set";
                } else if (result == '4') {
                    return "perm reset";
                }
            }
            return "error";
        };

        info_vec.emplace_back(i);
    };

    // Modenames mostly based on the fine documentation of xterm and some other sources
    // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html

    addMode("2"    , "Keyboard Action Mode          ");
    addMode("3"    , "Display control chars         ");
    addMode("4"    , "Insert Mode                   ");
    addMode("12"   , "Send/receive                  ");
    addMode("20"   , "Automatic Linefeed Mode       ");
    addMode("34"   , "Normal Cursor Visibility      ");

    addMode("?1"   , "Application Cursor Keys       ");
    addMode("?2"   , "ASCII (DECANM)                ");
    addMode("?3"   , "132 columns                   ");
    addMode("?4"   , "Smooth Scroll                 ");
    addMode("?5"   , "Reverse Video                 ");
    addMode("?6"   , "scroll region relative (DECOM)");
    addMode("?7"   , "Wrap Mode                     ");
    addMode("?8"   , "keyboard autorepeat           ");
    addMode("?9"   , "X10 mouse tracking            ");

    addMode("?10"  , "Show toolbar (rxvt)           ");
    addMode("?12"  , "Blinking Cursor               ");
    addMode("?18"  , "Print form feed               ");
    addMode("?19"  , "print extent is full screen   ");
    addMode("?25"  , "Visible Cursor                ");
    addMode("?30"  , "Show scrollbar (rxvt)         ");
    addMode("?35"  , "font-shifting functions (rxvt)");
    addMode("?38"  , "Tektronix                     ");
    addMode("?40"  , "Allow 80 ←→ 132 Mode          ");
    addMode("?41"  , "more(1) fix                   ");
    addMode("?42"  , "National Replacement Character set");
    addMode("?44"  , "Margin Bel                    ");
    addMode("?45"  , "Reverse-wraparound Mode       ");
    addMode("?46"  , "Logging                       ");
    addMode("?47"  , "Alternate Screen (47)         ");
    addMode("?66"  , "Application keypad            ");
    addMode("?67"  , "Backarrow key sends backspace ");
    addMode("?69"  , "left and right margin         ");
    addMode("?95"  , "keep screen when DECCOLM is set/reset");

    addMode("?1000", "VT200 mouse tracking          ");
    addMode("?1001", "Use Hilite Mouse Tracking     ");
    addMode("?1002", "Cell Motion Mouse Tracking    ");
    addMode("?1003", "All Motion Mouse Tracking     ");
    addMode("?1004", "FocusIn/FocusOut events       ");
    addMode("?1005", "Enable UTF-8 Mouse            ");
    addMode("?1006", "SGR Mouse                     ");
    addMode("?1007", "Alternate Scroll              ");
    addMode("?1010", "Scroll to bottom on tty output");
    addMode("?1011", "Scroll to bottom on key press ");
    addMode("?1015", "urxvt Mouse                   ");
    addMode("?1034", "meta key sets eighth  bit     ");
    addMode("?1035", "special modifiers for Alt and NumLock keys");
    addMode("?1036", "Send ESC when Meta modifies a key");
    addMode("?1037", "Send DEL from the editing-keypad Delete key");
    addMode("?1039", "Send ESC when Alt modifies a key");
    addMode("?1040", "Keep selection even if not highlighted");
    addMode("?1041", "CLIPBOARD selection           ");
    addMode("?1042", "Urgency window manager hint   ");
    addMode("?1043", "popOnBell                     ");
    addMode("?1044", "Reuse the most recent data copied to CLIPBOARD");
    addMode("?1047", "Alternate Screen (1047)       ");
    addMode("?1048", "Save cursor as in DECSC       ");
    addMode("?1049", "Alternate Screen (1049)       ");
    addMode("?1050", "terminfo/termcap function-key ");
    addMode("?1051", "Sun function-key              ");
    addMode("?1052", "HP function-key               ");
    addMode("?1053", "SCO function-key              ");
    addMode("?1060", "legacy keyboard emulation (X11R6)");
    addMode("?1061", "VT220 keyboard emulation      ");

    addMode("?2004", "bracketed paste               ");
    addMode("?2001", "click1 emit Esc seq to move point");
    addMode("?2002", "press2 emit Esc seq to move point");
    addMode("?2003", "Double click-3 deletes        ");
    addMode("?2005", "Quote each char during paste  ");
    addMode("?2006", "Paste '\\n' as C-j             ");

    kick();

    while (!finished) {
        char buff[100];
        int amount = read (STDIN_FILENO, buff, 99);
        termpaint_input_add_data(input, buff, amount);
    }

    tcsetattr (STDIN_FILENO, TCSAFLUSH, &otattr);

    return 0;
}
