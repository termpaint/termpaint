#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "termpaint.h"
#include "termpaintx.h"

void null_callback(void *ctx, termpaint_event *event) {

}

typedef struct {
    int id;
    const char *name;
    _Bool state;
} Cap;

Cap caps[] = {
#define C(name) { TERMPAINT_CAPABILITY_ ## name, #name, 0 }
    C(CSI_POSTFIX_MOD),
    C(TITLE_RESTORE),
    C(MAY_TRY_CURSOR_SHAPE_BAR),
    C(CURSOR_SHAPE_OSC50),
    C(EXTENDED_CHARSET),
    C(TRUECOLOR_MAYBE_SUPPORTED),
    C(TRUECOLOR_SUPPORTED),
    C(88_COLOR),
    C(CLEARED_COLORING),
    C(7BIT_ST),
#undef C
    { 0, NULL }
};

char *debug = NULL;

void debug_log(termpaint_integration *integration, const char *data, int length) {
    if (debug) {
        const int oldlen = strlen(debug);
        debug = realloc(debug, oldlen + length + 1);
        memcpy(debug + oldlen, data, length);
        debug[oldlen + length + 1] = 0;
    } else {
        debug = strndup(data, length);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration = termpaintx_full_integration_from_fd(1, 0, "+kbdsigint +kbdsigtstp");
    if (!integration) {
        puts("Could not init!");
        return 1;
    }

    termpaint_integration_set_logging_func(integration, debug_log);

    termpaint_terminal *terminal = termpaint_terminal_new(integration);
    termpaint_terminal_set_event_cb(terminal, null_callback, NULL);
    termpaintx_full_integration_set_terminal(integration, terminal);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                           "Terminal auto detection is taking unusually long, press space to abort.");

    char buff[1000];
    termpaint_terminal_auto_detect_result_text(terminal, buff, sizeof (buff));
    for (Cap *c = caps; c->name; c++) {
        c->state = termpaint_terminal_capable(terminal, c->id);
    }

    termpaint_terminal_free_with_restore(terminal);

    puts(buff);

    for (Cap *c = caps; c->name; c++) {
        printf("%s: %s\n", c->name, c->state ? "1" : "0");
    }

    for (int i=1; i < argc; i++) {
        if (strcmp(argv[i], "--write-file") == 0) {
            if (i + 1 < argc) {
                FILE *f = fopen(argv[i + 1], "w");
                if (!f) {
                    perror("fopen");
                } else {
                    fputs(buff, f);
                    fprintf(f, "\n");
                    for (Cap *c = caps; c->name; c++) {
                        fprintf(f, "%s: %s\n", c->name, c->state ? "1" : "0");
                    }
                    fclose(f);
                }
            }
        }
        if (strcmp(argv[i], "--debug") == 0) {
            printf("%s", debug);
        }
        if (strcmp(argv[i], "--key-wait") == 0) {
            puts("Press any key to continue");
            getchar();
        }
    }

    return 0;
}
