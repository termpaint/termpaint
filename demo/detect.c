// SPDX-License-Identifier: BSL-1.0
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "termpaint.h"
#include "termpaintx.h"

void null_callback(void *ctx, termpaint_event *event) {
    (void)ctx; (void)event;
}

typedef struct {
    int id;
    const char *name;
    const char *short_name;
    _Bool state;
} Cap;

Cap caps[] = {
#define C(name, s) { TERMPAINT_CAPABILITY_ ## name, #name, s, 0 }
    C(CSI_POSTFIX_MOD, "pf-mod"),
    C(TITLE_RESTORE, "title"),
    C(MAY_TRY_CURSOR_SHAPE_BAR, "cur-bar"),
    C(CURSOR_SHAPE_OSC50, "cur50"),
    C(EXTENDED_CHARSET, "extchset"),
    C(TRUECOLOR_MAYBE_SUPPORTED, "24maybe"),
    C(TRUECOLOR_SUPPORTED, "24sup"),
    C(88_COLOR, "88col"),
    C(CLEARED_COLORING, "clrcol"),
    C(7BIT_ST, "7bit-st"),
    C(MAY_TRY_TAGGED_PASTE, "taggedpaste"),
    C(CLEARED_COLORING_DEFCOLOR, "clrcoldef"),
#undef C
    { 0, NULL, 0, 0 }
};

char *debug = NULL;
bool debug_used = false;

void debug_log(termpaint_integration *integration, const char *data, int length) {
    (void)integration;
    if (debug_used && !debug) return; // memory allocaton failure
    if (debug) {
        const int oldlen = strlen(debug);
        char* debug_old = debug;
        debug = realloc(debug, oldlen + length + 1);
        if (debug) {
            memcpy(debug + oldlen, data, length);
            debug[oldlen + length] = 0;
        } else {
            free(debug_old);
        }
    } else {
        debug = strndup(data, length);
    }
    debug_used = true;
}

char *strdup_escaped(const char *tmp) {
    // escaping could quadruple size
    char *ret = malloc(strlen(tmp) * 4 + 1);
    if (!ret) {
        perror("malloc");
        abort();
    }
    char *dst = ret;
    for (; *tmp; tmp++) {
        if (*tmp >= ' ' && *tmp <= 126 && *tmp != '\\') {
            *dst = *tmp;
            ++dst;
        } else {
            dst += sprintf(dst, "\\x%02hhx", (unsigned char)*tmp);
        }
    }
    *dst = 0;
    return ret;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration = termpaintx_full_integration("+kbdsigint +kbdsigtstp");
    if (!integration) {
        puts("Could not init!");
        return 1;
    }

    termpaint_integration_set_logging_func(integration, debug_log);

    termpaint_terminal *terminal = termpaint_terminal_new(integration);
    termpaint_terminal_set_log_mask(terminal, TERMPAINT_LOG_AUTO_DETECT_TRACE | TERMPAINT_LOG_TRACE_RAW_INPUT);
    termpaint_terminal_set_event_cb(terminal, null_callback, NULL);
    termpaintx_full_integration_set_terminal(integration, terminal);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                           "Terminal auto detection is taking unusually long, press space to abort.");

    char buff[1000];
    char *self_reported_name_and_version = NULL;
    termpaint_terminal_auto_detect_result_text(terminal, buff, sizeof (buff));
    if (termpaint_terminal_self_reported_name_and_version(terminal)) {
        self_reported_name_and_version = strdup_escaped(termpaint_terminal_self_reported_name_and_version(terminal));
    }
    for (Cap *c = caps; c->name; c++) {
        c->state = termpaint_terminal_capable(terminal, c->id);
    }

    termpaint_terminal_free_with_restore(terminal);

    _Bool quiet = false;
    _Bool short_output = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) {
           quiet = true;
        }
        if (strcmp(argv[i], "--short") == 0) {
           short_output = true;
        }
    }

    if (!quiet && !short_output) {
        puts(buff);

        if (self_reported_name_and_version) {
            printf("self reported: %s\n", self_reported_name_and_version);
        }

        for (Cap *c = caps; c->name; c++) {
            printf("%s: %s\n", c->name, c->state ? "1" : "0");
        }
    }

    if (short_output) {
        printf("V1 %s", buff);
        if (self_reported_name_and_version) {
            printf(" >%s<", self_reported_name_and_version);
        }
        for (Cap *c = caps; c->name; c++) {
            if (c->state) {
                printf(" %s", c->short_name);
            }
        }
        puts("\n");
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

                    if (self_reported_name_and_version) {
                        fprintf(f, "self reported: %s\n", self_reported_name_and_version);
                    }

                    for (Cap *c = caps; c->name; c++) {
                        fprintf(f, "%s: %s\n", c->name, c->state ? "1" : "0");
                    }
                    fclose(f);
                }
            }
        }
        if (strcmp(argv[i], "--debug") == 0) {
            if (debug) {
                printf("%s", debug);
            } else if (debug_used) {
                printf("debug log could not be allocated!\n");
            }
        }
        if (strcmp(argv[i], "--key-wait") == 0) {
            puts("Press any key to continue");
            getchar();
        }
    }

    free(self_reported_name_and_version);

    return 0;
}
