#include <unistd.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>

#include <vector>
#include <string>

typedef bool _Bool;

#include "termpaint.h"
#include "termpaintx.h"
#include "termpaint_input.h"
#include "termpaintx_ttyrescue.h"

struct DisplayEvent {
    std::string raw;
    std::string eventString;
};

std::vector<DisplayEvent> ring;
std::string peek_buffer;
termpaint_terminal *terminal;
termpaint_surface *surface;
time_t last_q;
bool m_mode;
bool quit;
bool focus_tracking = false;
bool tagged_paste = false;
bool raw_paste = false;
bool legacy_mouse_support = false;
bool raw_command_mode = false;
std::string raw_commmand_str;
std::string terminal_info;


template <typename X>
unsigned char u8(X); // intentionally undefined

template <>
unsigned char u8(char ch) {
    return ch;
}

_Bool raw_filter(void *user_data, const char *data, unsigned length, _Bool overflow) {
    (void)user_data;
    std::string event { data, length };
    ring.emplace_back();
    ring.back().raw = event;

    if (event == "q") {
        time_t now = time(0);
        if (last_q && (now - last_q) == 3) {
            quit = true;
        }
        last_q = now;
    } else {
        last_q = 0;
    }

    if (m_mode) {
        m_mode = false;
        if (event == "0") {
            termpaint_terminal_set_mouse_mode(terminal, TERMPAINT_MOUSE_MODE_OFF);
        } else if (event == "1") {
            termpaint_terminal_set_mouse_mode(terminal, TERMPAINT_MOUSE_MODE_CLICKS);
        } else if (event == "2") {
            termpaint_terminal_set_mouse_mode(terminal, TERMPAINT_MOUSE_MODE_DRAG);
        } else if (event == "3") {
            termpaint_terminal_set_mouse_mode(terminal, TERMPAINT_MOUSE_MODE_MOVEMENT);
        } else if (event == "4") {
            focus_tracking = !focus_tracking;
            termpaint_terminal_request_focus_change_reports(terminal, focus_tracking);
        } else if (event == "6") {
            legacy_mouse_support = !legacy_mouse_support;
            termpaint_terminal_expect_legacy_mouse_reports(terminal, legacy_mouse_support);
        } else if (event == "p") {
            tagged_paste = !tagged_paste;
            termpaint_terminal_request_tagged_paste(terminal, tagged_paste);
        } else if (event == "r") {
            raw_paste = !raw_paste;
            termpaint_terminal_handle_paste(terminal, !raw_paste);
        } else if (event == "x") {
            raw_commmand_str = "";
            raw_command_mode = true;
        } else if (event == "q") {
            quit = true;
        }
    } else if (raw_command_mode) {
        if (event[1] == 0 && event[0] >= ' ' && event[0] <= 126) {
            raw_commmand_str += event;
        }
        if (event == "\x0d") {
            raw_command_mode = false;
            if (raw_commmand_str.size()) {
                printf("%s", "\033[0;0H\033");
                printf("%s", raw_commmand_str.data());
                fflush(stdout);
                sleep(1);
            }
        }
        if (event == "\x08" || event == "\x7f") {
            if (raw_commmand_str.size()) {
                raw_commmand_str.pop_back();
            }
        }

    } else {
        if (event == "m") {
            m_mode = true;
        }
    }

    return 0;
}

void event_handler(void *user_data, termpaint_event *event) {
    (void)user_data;
    std::string pretty;

    if (event->type == 0) {
        pretty = "unknown";
    } else if (event->type == TERMPAINT_EV_KEY) {
        pretty = "K: ";
        if ((event->key.modifier & ~(TERMPAINT_MOD_SHIFT|TERMPAINT_MOD_ALT|TERMPAINT_MOD_CTRL)) == 0) {
            pretty += (event->key.modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
            pretty += (event->key.modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
            pretty += (event->key.modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";
        } else {
            char buf[100];
            snprintf(buf, 100, "%03d", event->key.modifier);
            pretty += buf;
        }
        pretty += " ";
        pretty += std::string { event->key.atom, event->key.length };
    } else if (event->type == TERMPAINT_EV_CHAR) {
        pretty = "C: ";
        pretty += (event->c.modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
        pretty += (event->c.modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
        pretty += (event->c.modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";
        pretty += " ";
        pretty += std::string { event->c.string, event->c.length };
    } else if (event->type == TERMPAINT_EV_MOUSE) {
        if ((event->mouse.modifier & ~(TERMPAINT_MOD_SHIFT|TERMPAINT_MOD_ALT|TERMPAINT_MOD_CTRL)) == 0) {
            pretty += (event->mouse.modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
            pretty += (event->mouse.modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
            pretty += (event->mouse.modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";
        } else {
            char buf[100];
            snprintf(buf, 100, "%03d", event->mouse.modifier);
            pretty += buf;
        }
        pretty += " Mouse ";
        if (event->mouse.action == TERMPAINT_MOUSE_PRESS) {
            pretty += std::to_string(event->mouse.button) + " press";
        } else if (event->mouse.action == TERMPAINT_MOUSE_MOVE) {
            pretty += "move";
        } else if (event->mouse.button != 3) {
            pretty += std::to_string(event->mouse.button) + " release";
        } else {
            pretty += "some release";
        }
        pretty += ": x=" + std::to_string(event->mouse.x) + " y=" + std::to_string(event->mouse.y)
                + " rawbtn=" + std::to_string(event->mouse.raw_btn_and_flags);
    } else if (event->type == TERMPAINT_EV_MISC) {
        pretty += "Misc: ";
        pretty += std::string { event->misc.atom, event->misc.length };
    } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
        pretty = "Cursor position report: x=" + std::to_string(event->cursor_position.x) + " y=" + std::to_string(event->cursor_position.y);
    } else if (event->type == TERMPAINT_EV_MODE_REPORT) {
        if (event->mode.kind & 1) {
            pretty = "Mode status report: mode=?" + std::to_string(event->mode.number) + " status=" + std::to_string(event->mode.status);
        } else {
            pretty = "Mode status report: mode=" + std::to_string(event->mode.number) + " status=" + std::to_string(event->mode.status);
        }
    } else if (event->type == TERMPAINT_EV_PASTE) {
        pretty = "Paste: ";
        pretty += (event->paste.initial) ? "I" : " ";
        pretty += (event->paste.final) ? "F" : " ";
        pretty += " ";
        pretty += std::string { event->paste.string, event->paste.length };
    } else {
        pretty = "Other event no. " + std::to_string(event->type);
    }

    if (ring.empty() || ring.back().eventString.size()) {
        ring.emplace_back();
    }
    ring.back().eventString = pretty;
}

const auto rgb_white = TERMPAINT_RGB_COLOR(0xff, 0xff, 0xff);
const auto rgb_greyCC = TERMPAINT_RGB_COLOR(0xcc, 0xcc, 0xcc);
const auto rgb_grey7F = TERMPAINT_RGB_COLOR(0x7f, 0x7f, 0x7f);
const auto rgb_black = TERMPAINT_RGB_COLOR(0, 0, 0);
const auto rgb_redFF = TERMPAINT_RGB_COLOR(0xff, 0, 0);
const auto rgb_red7F = TERMPAINT_RGB_COLOR(0xff, 0, 0);

void display_esc(int x, int y, const std::string &data) {
    for (unsigned i = 0; i < data.length(); i++) {
        if (u8(data[i]) == '\e') {
            termpaint_surface_write_with_colors(surface, x, y, "^[", rgb_white, rgb_red7F);
            x+=2;
        } else if (0xfc == (0xfe & u8(data[i])) && i+5 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], data[i+4], data[i+5], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_grey7F);
            x += 1;
            i += 5;
        } else if (0xf8 == (0xfc & u8(data[i])) && i+4 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], data[i+4], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_grey7F);
            x += 1;
            i += 4;
        } else if (0xf0 == (0xf8 & u8(data[i])) && i+3 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_grey7F);
            x += 1;
            i += 3;
        } else if (0xe0 == (0xf0 & u8(data[i])) && i+2 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_grey7F);
            x += 1;
            i += 2;
        } else if (0xc0 == (0xe0 & u8(data[i])) && i+1 < data.length()) {
            if (((unsigned char)data[i]) == 0xc2 && ((unsigned char)data[i+1]) < 0xa0) { // C1 and non breaking space
                char v = ((unsigned char)data[i+1]) >> 4;
                char a = char(v < 10 ? '0' + v : 'a' + v - 10);
                v = data[i+1] & 0xf;
                char b = char(v < 10 ? '0' + v : 'a' + v - 10);
                char buf[7] = {'\\', 'u', '0', '0', a, b, 0};
                termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_red7F);
                x += 6;
            } else {
                char buf[7] = {data[i], data[i+1], 0};
                termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_grey7F);
                x += 1;
            }
            i += 1;
        } else if (data[i] < 32 || data[i] >= 127) {
            termpaint_surface_write_with_colors(surface, x, y, "\\x", rgb_white, rgb_red7F);
            x += 2;
            char buf[3];
            sprintf(buf, "%02x", (unsigned char)data[i]);
            termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_red7F);
            x += 2;
        } else {
            char buf[2] = {data[i], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, rgb_white, rgb_grey7F);
            x += 1;
        }
    }
}

void render() {
    termpaint_surface_clear(surface, rgb_white, rgb_black);

    termpaint_surface_write_with_colors(surface, 0, 0, "Input Decoding", rgb_white, rgb_black);
    termpaint_surface_write_with_colors(surface, 5, 23, "m for menu", rgb_white, rgb_black);
    termpaint_surface_write_with_colors(surface, 20, 0, terminal_info.data(), rgb_greyCC, rgb_black);

    if (peek_buffer.length()) {
        termpaint_surface_write_with_colors(surface, 0, 23, "unmatched:", rgb_redFF, rgb_black);
        display_esc(11, 23, peek_buffer);
    }

    int y = 2;
    for (DisplayEvent &event : ring) {
        display_esc(5, y, event.raw);
        termpaint_surface_write_with_colors(surface, 30, y, event.eventString.data(), rgb_redFF, rgb_black);
        ++y;
    }

    if (m_mode) {
        y = 10;
        termpaint_surface_write_with_colors(surface, 10, y++, "+ Choose:                    +", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| q: quit                    |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| 0: mouse off               |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| 1: mouse clicks on         |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| 2: mouse drag on           |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| 3: mouse movements on      |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| 4: toggle focus tracking   |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| 6: toggle legacy mouse sup |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| p: toggle tagged paste     |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| r: toggle tagged paste raw |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "| x: raw mode switch         |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, y++, "+----------------------------+", rgb_black, rgb_greyCC);
    }

    if (raw_command_mode) {
        termpaint_surface_write_with_colors(surface, 10, 10, "+ Sequence to send:                          +", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, 11, "| ESC                                        |", rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 15, 11, raw_commmand_str.data(), rgb_black, rgb_greyCC);
        termpaint_surface_write_with_colors(surface, 10, 12, "+--------------------------------------------+", rgb_black, rgb_greyCC);
    }

    termpaint_terminal_flush(terminal, false);
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    termpaint_integration *integration = termpaintx_full_integration_from_fd(1, 0, "+kbdsigint +kbdsigtstp");
    if (!integration) {
        puts("Could not init!");
        return 1;
    }

    terminal = termpaint_terminal_new(integration);
    termpaintx_full_integration_set_terminal(integration, terminal);
    surface = termpaint_terminal_get_surface(terminal);
    termpaint_terminal_set_raw_input_filter_cb(terminal, raw_filter, 0);
    termpaint_terminal_set_event_cb(terminal, event_handler, 0);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                           "Terminal auto detection is taking unusually long, press space to abort.");
    termpaintx_full_integration_apply_input_quirks(integration);
    int width, height;
    termpaintx_full_integration_terminal_size(integration, &width, &height);
    termpaint_terminal_setup_fullscreen(terminal, width, height, "+kbdsig");
    termpaintx_full_integration_ttyrescue_start(integration);

    if (termpaint_terminal_auto_detect_state(terminal) == termpaint_auto_detect_done) {
        char buff[100];
        termpaint_terminal_auto_detect_result_text(terminal, buff, sizeof (buff));
        terminal_info = std::string(buff);
    }

    render();
    while (!quit) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            break;
        }
        peek_buffer = std::string(termpaint_terminal_peek_input_buffer(terminal), termpaint_terminal_peek_input_buffer_length(terminal));

        while (ring.size() > 18) {
            ring.erase(ring.begin());
        }

        render();
    }

    termpaint_terminal_free_with_restore(terminal);

    return 0;
}
