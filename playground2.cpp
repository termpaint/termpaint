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

std::vector<std::string> ring;
std::vector<std::string> ring2;
std::string peek_buffer;
termpaint_surface *surface;
time_t last_q;
bool quit;


template <typename X>
unsigned char u8(X); // intentionally undefined

template <>
unsigned char u8(char ch) {
    return ch;
}

_Bool raw_filter(void *user_data, const char *data, unsigned length, _Bool overflow) {
    (void)user_data;
    std::string event { data, length };
    ring.emplace_back(event);

    if (event == "q") {
        time_t now = time(0);
        if (last_q && (now - last_q) == 3) {
            quit = true;
        }
        last_q = now;
    } else {
        last_q = 0;
    }
    return 0;
}

void event_handler(void *user_data, termpaint_input_event *event) {
    (void)user_data;
    std::string pretty;

    if (event->type == 0) {
        pretty = "unknown";
    } else if (event->type == TERMPAINT_EV_KEY) {
        pretty = "K: ";
        if ((event->modifier & ~(TERMPAINT_MOD_SHIFT|TERMPAINT_MOD_ALT|TERMPAINT_MOD_CTRL)) == 0) {
            pretty += (event->modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
            pretty += (event->modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
            pretty += (event->modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";
        } else {
            char buf[100];
            snprintf(buf, 100, "%03d", event->modifier);
            pretty += buf;
        }
        pretty += " ";
        pretty += event->atom_or_string;
    } else if (event->type == TERMPAINT_EV_CHAR) {
        pretty = "C: ";
        pretty += (event->modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
        pretty += (event->modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
        pretty += (event->modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";
        pretty += " ";
        pretty += std::string { event->atom_or_string, event->length };
    } else {
        pretty = "XXX";
    }

    ring2.emplace_back(pretty);
}

void display_esc(int x, int y, const std::string &data) {
    for (unsigned i = 0; i < data.length(); i++) {
        if (u8(data[i]) == '\e') {
            termpaint_surface_write_with_colors(surface, x, y, "^[", 0xffffff, 0x7f0000);
            x+=2;
        } else if (0xfc == (0xfe & u8(data[i])) && i+5 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], data[i+4], data[i+5], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 5;
        } else if (0xf8 == (0xfc & u8(data[i])) && i+4 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], data[i+4], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 4;
        } else if (0xf0 == (0xf8 & u8(data[i])) && i+3 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 3;
        } else if (0xe0 == (0xf0 & u8(data[i])) && i+2 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 2;
        } else if (0xc0 == (0xe0 & u8(data[i])) && i+1 < data.length()) {
            if (((unsigned char)data[i]) == 0xc2 && ((unsigned char)data[i+1]) < 0xa0) { // C1 and non breaking space
                char x = ((unsigned char)data[i+1]) >> 4;
                char a = char(x < 10 ? '0' + x : 'a' + x - 10);
                x = data[i+1] & 0xf;
                char b = char(x < 10 ? '0' + x : 'a' + x - 10);
                char buf[7] = {'\\', 'u', '0', '0', a, b, 0};
                termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f0000);
                x += 6;
            } else {
                char buf[7] = {data[i], data[i+1], 0};
                termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
                x += 1;
            }
            i += 1;
        } else if (data[i] < 32 || data[i] >= 127) {
            termpaint_surface_write_with_colors(surface, x, y, "\\x", 0xffffff, 0x7f0000);
            x += 2;
            char buf[3];
            sprintf(buf, "%02x", (unsigned char)data[i]);
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f0000);
            x += 2;
        } else {
            char buf[2] = {data[i], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
        }
    }
}

void render() {
    termpaint_surface_clear(surface, 0x1000000);

    termpaint_surface_write_with_colors(surface, 0, 0, "Input Decoding", 0x1000000, 0x1000000);

    if (peek_buffer.length()) {
        termpaint_surface_write_with_colors(surface, 0, 23, "unmatched:", 0xff0000, 0x1000000);
        display_esc(11, 23, peek_buffer);
    }

    int y = 2;
    for (std::string &event : ring) {
        display_esc(5, y, event);
        ++y;
    }

    y = 2;
    for (std::string &event : ring2) {
        termpaint_surface_write_with_colors(surface, 20, y, event.data(), 0xff0000, 0x1000000);
        ++y;
    }
    if (y > 20) {
        ring.erase(ring.begin());
        ring2.erase(ring2.begin());
    }

    termpaint_surface_flush(surface);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration = termpaint_full_integration_from_fd(1, 0);
    if (!integration) {
        puts("Could not init!");
        return 1;
    }

    // xterm modify other characters: "\e[>4;2m" (disables ctrl-c)
    printf("%s", "\e[?66h");fflush(stdout);
    printf("%s", "\e[?1034l");fflush(stdout);
    printf("%s", "\e[?1036h");fflush(stdout);
    printf("%s", "\e[?1049h");fflush(stdout);

    surface = termpaint_surface_new(integration);
    termpaint_auto_detect(surface);
    termpaint_full_integration_poll_ready(integration);

    termpaint_surface_resize(surface, 80, 24);
    termpaint_surface_clear(surface, 0x1000000);
    //termpaint_surface_write_with_colors(surface, 0, 0, "Hallo müde", 0xff0000, 0x00ff00);

    termpaint_surface_flush(surface);

    termpaint_input *input = termpaint_input_new();
    termpaint_input_set_raw_filter_cb(input, raw_filter, 0);
    termpaint_input_set_event_cb(input, event_handler, 0);

    struct termios tattr;

    tcgetattr (STDIN_FILENO, &tattr);
    tattr.c_iflag |= IGNBRK;
    tattr.c_iflag &= ~(BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF);
    tattr.c_oflag &= ~(OPOST|ONLCR|OCRNL|ONOCR|ONLRET);
    tattr.c_lflag &= ~(ICANON|IEXTEN|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tattr.c_cc[VQUIT] = 0;
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);

    while (!quit) {
        char buff[100];
        int amount = read (STDIN_FILENO, buff, 99);
        termpaint_input_add_data(input, buff, amount);
        peek_buffer = std::string(termpaint_input_peek_buffer(input), termpaint_input_peek_buffer_length(input));
        if (peek_buffer.size()) {
            write(0, "\e[5n", 4);
        }
        render();
    }

    printf("%s", "\e[?66;1049l");fflush(stdout);

    return 0;
}
