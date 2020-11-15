// Feel free to copy from this example to your own code
// SPDX-License-Identifier: 0BSD OR BSL-1.0 OR MIT-0

#include <stdbool.h>

// snippet-header-start
#include "termpaint.h"
#include "termpaintx.h"
// snippet-header-end

// snippet-callback-start
void event_callback(void *userdata, termpaint_event *event) {
    bool *quit = userdata;
    if (event->type == TERMPAINT_EV_CHAR) {
        if (event->c.length == 1 && event->c.string[0] == 'q') {
            *quit = true;
        }
    }
    if (event->type == TERMPAINT_EV_KEY) {
        if (event->key.atom == termpaint_input_escape()) {
            *quit = true;
        }
    }
}
// snippet-callback-end

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    // snippet-main-start
    termpaint_integration *integration;
    termpaint_terminal *terminal;
    termpaint_surface *surface;

    bool quit = false;

    integration = termpaintx_full_integration_setup_terminal_fullscreen(
                "+kbdsig +kbdsigint",
                event_callback, &quit,
                &terminal);
    surface = termpaint_terminal_get_surface(terminal);
    termpaint_surface_clear(surface,
                TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(surface,
                0, 0,
                "Hello World",
                TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(terminal, false);

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
