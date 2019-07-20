#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "termpaint.h"
#include "termpaintx.h"
#include "termpaintx_ttyrescue.h"

termpaint_terminal *terminal;
termpaint_surface *surface;
termpaint_integration *integration;
int rescue_fd;

bool quit;

typedef struct event_ {
    int type;
    int modifier;
    const char *string;
    struct event_* next;
} event;

event* event_current;

void event_callback(void *userdata, termpaint_event *tp_event) {
    // remember tp_event is only valid while this callback runs, so copy everything we need.
    event *my_event = NULL;
    if (tp_event->type == TERMPAINT_EV_CHAR) {
        my_event = malloc(sizeof(event));
        my_event->type = tp_event->type;
        my_event->modifier = tp_event->c.modifier;
        my_event->string = strndup(tp_event->c.string, tp_event->c.length);
        my_event->next = NULL;
    } else if (tp_event->type == TERMPAINT_EV_KEY) {
        my_event = malloc(sizeof(event));
        my_event->type = tp_event->type;
        my_event->modifier = tp_event->key.modifier;
        my_event->string = strdup(tp_event->key.atom);
        my_event->next = NULL;
    }

    if (my_event) {
        event* prev = event_current;
        while (prev->next) {
            prev = prev->next;
        }
        prev->next = my_event;
    }
}

bool init(void) {
    event_current = malloc(sizeof(event));
    event_current->next = NULL;
    event_current->string = NULL;

    integration = termpaintx_full_integration_from_fd(1, 0, "+kbdsigint +kbdsigtstp");
    if (!integration) {
        puts("Could not init!");
        return 0;
    }

    terminal = termpaint_terminal_new(integration);
    termpaint_terminal_set_event_cb(terminal, event_callback, NULL);
    termpaintx_full_integration_set_terminal(integration, terminal);
    surface = termpaint_terminal_get_surface(terminal);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready(integration);
    int width, height;
    termpaintx_full_integration_terminal_size(integration, &width, &height);
    termpaint_terminal_setup_fullscreen(terminal, width, height, "+kbdsig");
    rescue_fd = termpaint_ttyrescue_start(termpaint_terminal_restore_sequence(terminal));

    return 1;
}

void cleanup(void) {
    termpaint_terminal_free_with_restore(terminal);
    termpaint_ttyrescue_stop(rescue_fd);
}

event* key_wait(void) {
    termpaint_terminal_flush(terminal, false);

    while (!event_current->next) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            cleanup();
            exit(1);
        }
    }

    free(event_current->string);
    event* next = event_current->next;
    free(event_current);
    event_current = next;
    return next;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (!init()) {
        return 1;
    }

    termpaint_surface_clear(surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(surface, 10, 3, "Samples:", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    // isolated U+0308 COMBINING DIAERESIS
    termpaint_surface_write_with_colors(surface, 10, 4, "\xcc\x88X", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    // 'a' + U+0308 COMBINING DIAERESIS
    termpaint_surface_write_with_colors(surface, 10, 5, "a\xcc\x88X", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    // 'a' + U+0308 COMBINING DIAERESIS + U+0324 COMBINING DIAERESIS BELOW
    termpaint_surface_write_with_colors(surface, 10, 6, "a\xcc\x88\xcc\xa4X", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    // 'a' + U+E0100 VARIATION SELECTOR-17 + U+E0101 VARIATION SELECTOR-18 (nonsense)
    termpaint_surface_write_with_colors(surface, 10, 7, "a\xf3\xa0\x84\x80\xf3\xa0\x84\x81X", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    // 'a' + U+E0100 VARIATION SELECTOR-17 + U+FE00 VARIATION SELECTOR-1 (nonsense)
    termpaint_surface_write_with_colors(surface, 10, 8, "a\xf3\xa0\x84\x80\xef\xb8\x80X", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    // 'a' + U+E0100 VARIATION SELECTOR-17 + U+FEFF ZERO WIDTH NO-BREAK SPACE (nonsense)
    termpaint_surface_write_with_colors(surface, 10, 9, "a\xf3\xa0\x84\x80\xef\xbb\xbfX", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_write_with_colors(surface, 10, 10, "あ3あ67あX", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    key_wait();

    cleanup();
    return 0;
}
