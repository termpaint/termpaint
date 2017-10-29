#include <unistd.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include "termpaint.h"
#include "termpaintx.h"
#include "termpaint_input.h"

_Bool raw_filter(void *user_data, const char *data, int length) {
    (void)user_data;
    for (int i = 0; i < length; i++) {
        if (data[i] == '\e') {
            write(1, "^[", 2);
        } else if (data[i] < 32 || data[i] >= 127) {
            write(1, "^x", 2);
            char buf[3];
            sprintf(buf, "%02x", (unsigned char)data[i]);
            write(1, buf, 2);
        } else {
            write(1, data+i, 1);
        }
    }
    write(1, "\n", 1);
    return 1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration = termpaint_full_integration_from_fd(1, 0);
    if (!integration) {
        puts("Could not init!");
        return 1;
    }

    termpaint_surface *surface = termpaint_surface_new(integration);
    termpaint_auto_detect(surface);
    termpaint_full_integration_poll_ready(integration);

    termpaint_surface_resize(surface, 80, 24);
    termpaint_surface_clear(surface, 0x1000000);
    termpaint_surface_write_with_colors(surface, 0, 0, "Hallo müde", 0xff0000, 0x00ff00);

    termpaint_surface_flush(surface);

    termpaint_input *input = termpaint_input_new();
    termpaint_input_set_raw_filter_cb(input, raw_filter, 0);

    struct termios tattr;

    tcgetattr (STDIN_FILENO, &tattr);
    tattr.c_iflag |= IGNBRK|IGNPAR;
    tattr.c_iflag &= ~(BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF);
    tattr.c_oflag &= ~(OPOST|ONLCR|OCRNL|ONOCR|ONLRET);
    tattr.c_lflag &= ~(ICANON|IEXTEN|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);

    while (1) {
        char buff[100];
        int amount = read (STDIN_FILENO, buff, 99);
        termpaint_input_add_data(input, buff, amount);
    }


    return 0;
}
