// Feel free to copy from this example to your own code
// SPDX-License-Identifier: 0BSD OR BSL-1.0 OR MIT-0

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "termpaint.h"
#include "termpaintx.h"


typedef struct event_ {
    int type;
    int modifier;
    char *string;
    int x;
    int y;
    struct event_* next;
} event;

event* event_current; // unprocessed event
event* event_saved; // event to free on next call

void event_callback(void *userdata, termpaint_event *tp_event) {
    (void)userdata;
    // remember tp_event is only valid while this callback runs, so copy everything we need.
    event *copied_event = NULL;
    if (tp_event->type == TERMPAINT_EV_CHAR) {
        copied_event = malloc(sizeof(event));
        copied_event->type = tp_event->type;
        copied_event->modifier = tp_event->c.modifier;
        copied_event->string = strndup(tp_event->c.string, tp_event->c.length);
        copied_event->next = NULL;
    } else if (tp_event->type == TERMPAINT_EV_KEY) {
        copied_event = malloc(sizeof(event));
        copied_event->type = tp_event->type;
        copied_event->modifier = tp_event->key.modifier;
        copied_event->string = strndup(tp_event->key.atom, tp_event->key.length);
        copied_event->next = NULL;
    } else if (tp_event->type == TERMPAINT_EV_MOUSE) {
        copied_event = malloc(sizeof(event));
        copied_event->type = tp_event->type;
        copied_event->modifier = tp_event->mouse.modifier;
        copied_event->string = NULL;
        copied_event->x = tp_event->mouse.x;
        copied_event->y = tp_event->mouse.y;
        copied_event->next = NULL;
    }

    if (copied_event) {
        if (!event_current) {
            event_current = copied_event;
        } else {
            event* prev = event_current;
            while (prev->next) {
                prev = prev->next;
            }
            prev->next = copied_event;
        }
    }
}

event* key_wait(termpaint_integration *integration) {
    while (!event_current) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            return NULL; // or some other error handling
        }
    }

    if (event_saved) {
        free(event_saved->string);
        free(event_saved);
    }
    event *ret = event_current;
    event_current = ret->next;
    event_saved = ret;
    return ret;
}

const int screen_bg = TERMPAINT_COLOR_BRIGHT_YELLOW;
const int ui_fg = TERMPAINT_COLOR_BLACK;
const int win_message = TERMPAINT_COLOR_GREEN;

const int tile_border = TERMPAINT_COLOR_BLACK;
const int tile_background = TERMPAINT_COLOR_LIGHT_GREY;

int field[5][5];
int x, y;

int current_start_x;
int current_start_y;

void solved_message(termpaint_surface *surface) {
    const int screen_width = termpaint_surface_width(surface);
    const int screen_height = termpaint_surface_height(surface);

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 12, screen_height / 2 - 2,
                                        "┌───────────────────────┐",
                                        ui_fg, win_message);

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 12, screen_height / 2 - 1,
                                        "│        Solved!        │",
                                        ui_fg, win_message);

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 12, screen_height / 2,
                                        "│                       │",
                                        ui_fg, win_message);

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 12, screen_height / 2 + 1,
                                        "│ Press any key to exit │",
                                        ui_fg, win_message);

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 12, screen_height / 2 + 2,
                                        "└───────────────────────┘",
                                        ui_fg, win_message);
}

void draw_screen(termpaint_surface *surface) {
    char buf[100];

    termpaint_surface_clear(surface,
                TERMPAINT_COLOR_BLACK, screen_bg);

    const int screen_width = termpaint_surface_width(surface);
    const int screen_height = termpaint_surface_height(surface);

    current_start_x = screen_width / 2 - 10;
    current_start_y = screen_height / 2 - 7;

    for (int x = 0; x < 5; x++) {
        for (int y = 0; y < 5; y++) {
            const int visual_x = current_start_x + x * 4;
            const int visual_y = current_start_y + y * 3;
            if (field[x][y] != -1) {
                termpaint_surface_write_with_colors(surface,
                                                    visual_x, visual_y,
                                                    "┌──┐",
                                                    tile_border, tile_background);
                termpaint_surface_write_with_colors(surface,
                                                    visual_x, visual_y + 1,
                                                    "│  │",
                                                    tile_border, tile_background);
                sprintf(buf, "%.2i", field[x][y]);
                int fg = (field[x][y] != y * 5 + x + 1) ? TERMPAINT_COLOR_RED
                                                        : TERMPAINT_COLOR_GREEN;
                termpaint_surface_write_with_colors(surface,
                                                    visual_x + 1, visual_y + 1,
                                                    buf,
                                                    fg, tile_background);
                termpaint_surface_write_with_colors(surface,
                                                    visual_x, visual_y + 2,
                                                    "└──┘",
                                                    tile_border, tile_background);
            } else {
                termpaint_surface_write_with_colors(surface,
                                                    visual_x + 1, visual_y,
                                                    "↓", ui_fg, screen_bg);
                termpaint_surface_write_with_colors(surface,
                                                    visual_x, visual_y + 1,
                                                    "→  ←", ui_fg, screen_bg);
                termpaint_surface_write_with_colors(surface,
                                                    visual_x + 2, visual_y + 2,
                                                    "↑", ui_fg, screen_bg);
            }
        }
    }

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 15, 0,
                                        "Use arrow keys to move tiles.", ui_fg, screen_bg);

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 15, 1,
                                        "Or click on the tile to move.", ui_fg, screen_bg);

    termpaint_surface_write_with_colors(surface,
                                        screen_width / 2 - 8, screen_height - 1,
                                        "Press Q to quit.", ui_fg, screen_bg);

}

enum direction {
    UP, RIGHT, DOWN, LEFT
};

bool do_move(int direction) {
    bool did_step = false;
    if (direction == UP && y > 0) {
        field[x][y] = field[x][y - 1];
        y -= 1;
        did_step = true;
    }
    if (direction == RIGHT && x < 4) {
        field[x][y] = field[x + 1][y];
        x += 1;
        did_step = true;
    }
    if (direction == DOWN && y < 4) {
        field[x][y] = field[x][y + 1];
        y += 1;
        did_step = true;
    }
    if (direction == LEFT && x > 0) {
        field[x][y] = field[x - 1][y];
        x -= 1;
        did_step = true;
    }
    if (did_step) {
        field[x][y] = -1;
    }
    return did_step;
}

void randomize(void) {
    for (int i = 0; i < 10; i++) {
        bool did_step = false;
        while (!did_step) {
            int direction = rand() % 4;
            did_step = do_move(direction);
        }
    }
}

bool solved(void) {
    for (int x = 0; x < 5; x++) {
        for (int y = 0; y < 5; y++) {
            if (x == 4 && y == 4) {
                return true;
            }
            if (field[x][y] != y * 5 + x + 1) {
                return false;
            }
        }
    }
    return false;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration;
    termpaint_terminal *terminal;
    termpaint_surface *surface;

    integration = termpaintx_full_integration_setup_terminal_fullscreen(
                "+kbdsig +kbdsigint",
                event_callback, NULL,
                &terminal);
    termpaint_terminal_set_mouse_mode(terminal, TERMPAINT_MOUSE_MODE_CLICKS);

    surface = termpaint_terminal_get_surface(terminal);

    for (int x = 0; x < 5; x++) {
        for (int y = 0; y < 5; y++) {
            field[x][y] = y * 5 + x + 1;
        }
    }
    field[4][4] = -1;

    x = 4;
    y = 4;

    randomize();

    while (!solved()) {
        draw_screen(surface);
        termpaint_terminal_flush(terminal, false);
        event* ev = key_wait(integration);
        if (ev->type == TERMPAINT_EV_KEY) {
            if (strcmp(ev->string, "ArrowUp") == 0) {
                do_move(DOWN);
            }
            if (strcmp(ev->string, "ArrowRight") == 0) {
                do_move(LEFT);
            }
            if (strcmp(ev->string, "ArrowDown") == 0) {
                do_move(UP);
            }
            if (strcmp(ev->string, "ArrowLeft") == 0) {
                do_move(RIGHT);
            }
        }
        if (ev->type == TERMPAINT_EV_CHAR) {
            if (strcmp(ev->string, "Q") == 0 || strcmp(ev->string, "q") == 0) {
                break;
            }
        }
        if (ev->type == TERMPAINT_EV_MOUSE) {
            int mouse_x = (ev->x - current_start_x) / 4;
            int mouse_y = (ev->y - current_start_y) / 3;

            if (mouse_x >= 0 && mouse_x <= 4 && mouse_y >= 0 && mouse_y <= 4) {
                if (mouse_x == x) {
                    if (mouse_y == y - 1) {
                        do_move(UP);
                    }
                    if (mouse_y == y + 1) {
                        do_move(DOWN);
                    }
                }
                if (mouse_y == y) {
                    if (mouse_x == x - 1) {
                        do_move(LEFT);
                    }
                    if (mouse_x == x + 1) {
                        do_move(RIGHT);
                    }
                }
            }
        }
    }
    if (solved()) {
        draw_screen(surface);
        solved_message(surface);
        termpaint_terminal_flush(terminal, false);
        key_wait(integration);
    }

    termpaint_terminal_free_with_restore(terminal);

    return 0;
}
