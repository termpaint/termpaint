// Feel free to copy from this example to your own code
// SPDX-License-Identifier: 0BSD OR BSL-1.0 OR MIT-0

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "termpaint.h"
#include "termpaintx.h"

termpaint_integration *integration;
termpaint_terminal *terminal;
termpaint_surface *surface;
bool update;

static int min(int a, int b) {
    return (a < b) ? a : b;
}

enum cell_t { DEAD, ALIVE };

typedef struct board_t_ {
    int width;
    int height;

    char *cells;
} board_t;

board_t board;

bool pause;
int current_background;
int generation;
int refresh = 250;
int steps = 1;
int speed = 4; // > 0 then steps per second, otherwise -speed + 2 seconds per step

int cursor_x = 0;
int cursor_y = 1;

char *cell_at(board_t *board, int x, int y) {
    return &board->cells[((board->height + y) % board->height) * board->width + ((board->width + x) % board->width)];
}

void update_timing(void) {
    steps = 1;
    if (speed > 0) {
        float r = 1. / speed;
        while (r < .1) {
            r *= 2;
            steps *= 2;
        }
        refresh = 1000 * r;
    } else {
        refresh = 1000 * (-speed + 2);
    }
}

void event_callback(void *userdata, termpaint_event *event) {
    bool *quit = userdata;
    if (event->type == TERMPAINT_EV_CHAR) {
        if (event->c.length == 1 && event->c.string[0] == 'q') {
            *quit = true;
        }
        if (event->c.length == 1 && event->c.string[0] == '+') {
            speed += 1;
            update_timing();
            update = true;
        }
        if (event->c.length == 1 && event->c.string[0] == '-') {
            speed -= 1;
            update_timing();
            update = true;
        }
        if (event->c.length == 1 && event->c.string[0] == '0') {
            *cell_at(&board, cursor_x, cursor_y) = DEAD;
            update = true;
        }
        if (event->c.length == 1 && event->c.string[0] == '1') {
            *cell_at(&board, cursor_x, cursor_y) = ALIVE;
            update = true;
        }
    }
    if (event->type == TERMPAINT_EV_KEY) {
        if (event->key.atom == termpaint_input_space()) {
            pause = !pause;
            update = true;
        } else if (event->key.atom == termpaint_input_arrow_up()) {
            cursor_y = (board.height + cursor_y - 1) % board.height;
            update = true;
        } else if (event->key.atom == termpaint_input_arrow_down()) {
            cursor_y = (cursor_y + 1) % board.height;
            update = true;
        } else if (event->key.atom == termpaint_input_arrow_left()) {
            cursor_x = (board.width + cursor_x - 1) % board.width;
            update = true;
        } else if (event->key.atom == termpaint_input_arrow_right()) {
            cursor_x = (cursor_x + 1) % board.width;
            update = true;
        }
    }
    if (event->type == TERMPAINT_EV_MOUSE) {
        if ((event->mouse.action == TERMPAINT_MOUSE_PRESS && event->mouse.button == 0)
            || event->mouse.action == TERMPAINT_MOUSE_MOVE) {
            cursor_x = event->mouse.x;
            cursor_y = event->mouse.y;
            char *cell = cell_at(&board, event->mouse.x, event->mouse.y);
            *cell = !*cell;
            update = true;
        }
    }
}

int rule(board_t *b, int x, int y) {
    int count = *cell_at(b, x - 1, y - 1) + *cell_at(b, x, y - 1) + *cell_at(b, x + 1, y - 1)
              + *cell_at(b, x - 1, y)                             + *cell_at(b, x + 1, y)
              + *cell_at(b, x - 1, y + 1) + *cell_at(b, x, y + 1) + *cell_at(b, x + 1, y + 1);

    int self = *cell_at(b, x, y);
    if (count == 3) {
        return ALIVE;
    }
    if (self && count == 2) {
        return ALIVE;
    }
    return DEAD;
}

void pulse(void) {
    if (pause) {
        return;
    }

    static int i = 0;

    update = true;

    int g;

    if (i < 60) {
        g = i;
    } else {
        g = 60 - (i - 60);
    }

    current_background = TERMPAINT_RGB_COLOR(0, 30 + g, 0);

    for (int i = 0; i < steps; i++) {
        board_t next;
        next.width = termpaint_surface_width(surface);
        next.height = termpaint_surface_height(surface);
        next.cells = calloc(next.width * next.height, 1);

        int count = 0;

        for (int x = 0; x < min(board.width, next.width); x++) {
            for (int y = 0; y < min(board.height, next.width); y++) {
                int new_state = rule(&board, x, y);
                *cell_at(&next, x, y) = new_state;
                count += new_state;
            }
        }

        if (count == 0) {
            if (next.height > 5 && next.width > 5) {
                *cell_at(&next, next.width / 2, next.height / 2 - 1) = ALIVE;
                *cell_at(&next, next.width / 2 + 1, next.height / 2) = ALIVE;
                *cell_at(&next, next.width / 2, next.height / 2 + 1) = ALIVE;
                *cell_at(&next, next.width / 2 - 1, next.height / 2 + 1) = ALIVE;
                *cell_at(&next, next.width / 2 + 1, next.height / 2 + 1) = ALIVE;
            }
        }

        board.width = next.width;
        board.height = next.height;
        free(board.cells);
        board.cells = next.cells;

        generation += 1;
    }

    i = (i + 10) % 120;
}

void redraw(void) {
    int cell_color = TERMPAINT_RGB_COLOR(255, 255, 255);

    termpaint_surface_clear(surface, TERMPAINT_DEFAULT_COLOR, current_background);

    for (int x = 0; x < board.width; x++) {
        for (int y = 0; y < board.height; y++) {
            int cell_background = current_background;
            if (pause && cursor_x == x && cursor_y == y) {
                cell_background = TERMPAINT_RGB_COLOR(0, 0, 0xdd);
                termpaint_surface_write_with_colors(surface,
                            x, y,
                            " ",
                            cell_color, cell_background);
            }
            if (*cell_at(&board, x,y)) {
                termpaint_surface_write_with_colors(surface,
                            x, y,
                            "♦",
                            cell_color, cell_background);
            }
        }
    }

    if (pause) {
        termpaint_surface_write_with_colors(surface,
                    0, 0,
                    "q to quit, space to pause, cursor keys and 0/1 or mouse to edit",
                    cell_color, current_background);
    } else {
        termpaint_surface_write_with_colors(surface,
                    0, 0,
                    "q to quit, space to pause, -/+ change speed, mouse to edit",
                    cell_color, current_background);
    }

    char buf[1000];

    if (pause) {
        sprintf(buf, "generation: %i, speed %i (paused)", generation, speed);
    } else {
        sprintf(buf, "generation: %i, speed %i", generation, speed);
    }

    termpaint_surface_write_with_colors(surface,
                0, board.height - 1,
                buf,
                cell_color, current_background);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    bool quit = false;

    integration = termpaintx_full_integration_setup_terminal_fullscreen(
                "+kbdsig +kbdsigint",
                event_callback, &quit,
                &terminal);
    surface = termpaint_terminal_get_surface(terminal);

    termpaint_terminal_set_mouse_mode(terminal, TERMPAINT_MOUSE_MODE_DRAG);

    board.width = termpaint_surface_width(surface);
    board.height = termpaint_surface_height(surface);

    board.cells = calloc(board.width * board.height, 1);

    pulse();

    int timeout = refresh;

    update = true;

    while (!quit) {
        if (update) {
            redraw();
            termpaint_terminal_flush(terminal, false);
            update = false;
        }
        if (!termpaintx_full_integration_do_iteration_with_timeout(integration, &timeout)) {
            // some kind of error
            break;
        }
        if (timeout == 0) {
            pulse();
            timeout = refresh;
        }
    }

    termpaint_terminal_free_with_restore(terminal);

    return 0;
}
