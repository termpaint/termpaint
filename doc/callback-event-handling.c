// Feel free to copy from this example to your own code
// SPDX-License-Identifier: 0BSD OR BSL-1.0 OR MIT-0

#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

// snippet-header-start
#include "termpaint.h"
#include "termpaintx.h"
// snippet-header-end

// snippet-globals-start
termpaint_terminal *terminal;
termpaint_surface *surface;

void (*current_callback)(void *user_data, termpaint_event* event);
void *current_data;
// snippet-globals-end

// snippet-callback-start
void event_callback(void *userdata, termpaint_event *event) {
    (void)userdata;
    if (current_callback) {
        current_callback(current_data, event);
    }
}
// snippet-callback-end

// snippet-quit-data-start
typedef struct quit_dialog_ {
    void (*saved_callback)(void *user_data, termpaint_event* event);
    void *saved_data;
    bool *result;
} quit_dialog;
// snippet-quit-data-end

// snippet-quit-callback-start
void quit_dialog_callback(void *userdata, termpaint_event *event) {
    quit_dialog* dlg = userdata;

    if (!event) {
        return;
    }

    if (event->type == TERMPAINT_EV_CHAR) {
        if (event->c.length == 1
                && (event->c.string[0] == 'y' || event->c.string[0] == 'Y')) {
            current_data = dlg->saved_data;
            current_callback = dlg->saved_callback;
            *dlg->result = true;
            free(dlg);
            current_callback(current_data, NULL);
            return;
        }
        if (event->c.length == 1
                && (event->c.string[0] == 'n' || event->c.string[0] == 'N')) {
            current_data = dlg->saved_data;
            current_callback = dlg->saved_callback;
            *dlg->result = false;
            free(dlg);
            current_callback(current_data, NULL);
            return;
        }
    }
    if (event->type == TERMPAINT_EV_CHAR || event->type == TERMPAINT_EV_KEY) {
        termpaint_surface_write_with_colors(surface, 20, 5,
                    "Please reply with either 'y' for yes or 'n' for no.",
                    TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        termpaint_terminal_flush(terminal, false);
    }
}
// snippet-quit-callback-end

// snippet-quit-ctor-start
void quit_dialog_start(bool *result) {
    quit_dialog* dlg = calloc(1, sizeof(quit_dialog));
    dlg->saved_data = current_data;
    dlg->saved_callback = current_callback;
    dlg->result = result;

    current_data = dlg;
    current_callback = quit_dialog_callback;

    int fg = TERMPAINT_DEFAULT_COLOR;
    int bg = TERMPAINT_DEFAULT_COLOR;

    termpaint_surface_write_with_colors(surface, 20, 4, "Really quit? (y/N)",
                                        fg, bg);

    termpaint_terminal_flush(terminal, false);
}
// snippet-quit-ctor-end

void main_screen_callback(void *userdata, termpaint_event *event) {
    if (!event) {
        termpaint_surface_write_with_colors(surface,
                    0, 0,
                    "There is really nothing else to do than quit!",
                    TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        termpaint_terminal_flush(terminal, false);

        quit_dialog_start(userdata);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration;

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

    termpaint_terminal_flush(terminal, false);

    bool quit;

    current_callback = main_screen_callback;
    current_data = &quit;

    quit_dialog_start(&quit);

    // snippet-main-start
    while (!quit) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            break;
        }
    }

    termpaint_terminal_free_with_restore(terminal);
    // snippet-main-end

    return 0;
}
