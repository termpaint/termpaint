// Feel free to copy from this example to your own code
// SPDX-License-Identifier: 0BSD OR BSL-1.0 OR MIT-0

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "termpaint.h"
#include "termpaintx.h"


// snippet-type-start
typedef struct event_ {
    int type;
    int modifier;
    char *string;
    struct event_* next;
} event;

event* event_current; // unprocessed event
event* event_saved; // event to free on next call
// snippet-type-end

// snippet-callback-start
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
// snippet-callback-end

// snippet-wait-start
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
// snippet-wait-end

// snippet-menu-start
bool quit_menu(termpaint_terminal *terminal, termpaint_integration *integration) {
    termpaint_surface *surface;
    surface = termpaint_terminal_get_surface(terminal);

    int fg = TERMPAINT_DEFAULT_COLOR;
    int bg = TERMPAINT_DEFAULT_COLOR;

    termpaint_surface_write_with_colors(surface, 20, 4, "Really quit? (y/N)",
                                        fg, bg);

    termpaint_terminal_flush(terminal, false);

    while (true) {
        event *event = key_wait(integration);
        if (event->type == TERMPAINT_EV_CHAR) {
            if (!strcmp(event->string, "y") || !strcmp(event->string, "Y")) {
                return true;
            }
            if (!strcmp(event->string, "n") || !strcmp(event->string, "N")) {
                return false;
            }
        }
        termpaint_surface_write_with_colors(surface, 20, 5,
                    "Please reply with either 'y' for yes or 'n' for no.",
                    fg, bg);

        termpaint_terminal_flush(terminal, false);
    }
}
// snippet-menu-end

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration;
    termpaint_terminal *terminal;
    termpaint_surface *surface;

    integration = termpaintx_full_integration_setup_terminal_fullscreen(
                "+kbdsig +kbdsigint",
                event_callback, NULL,
                &terminal);
    surface = termpaint_terminal_get_surface(terminal);
    termpaint_surface_clear(surface,
                TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(surface,
                0, 0,
                "Hello World",
                TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    // snippet-main-start
    termpaint_terminal_flush(terminal, false);

    while (true) {
        event *event = key_wait(integration);
        if (event->type == TERMPAINT_EV_CHAR) {
            if (strcmp(event->string, "q") == 0) {
                break;
            }
        }
        if (event->type == TERMPAINT_EV_KEY) {
            if (strcmp(event->string, "Escape") == 0) {
                break;
            }
        }
    }
    // snippet-main-end

    quit_menu(terminal, integration);

    termpaint_terminal_free_with_restore(terminal);

    return 0;
}
