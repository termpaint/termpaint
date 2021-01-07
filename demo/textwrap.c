// Feel free to copy from this example to your own code
// SPDX-License-Identifier: 0BSD OR BSL-1.0 OR MIT-0

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "termpaint.h"
#include "termpaintx.h"

void event_callback(void *userdata, termpaint_event *event) {
    bool *quit = userdata;
    if (event->type == TERMPAINT_EV_CHAR) {
        *quit = true;
    }
    if (event->type == TERMPAINT_EV_KEY) {
        *quit = true;
    }
}

int main(int argc, char **argv) {

    if (argc != 2) {
        printf("Usage: %s filename\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror("Error opening file");
        return 1;
    }

    char *buffer = calloc(40000, 1);
    if (!buffer) {
        puts("Out of memory\n");
        return 1;
    }

    (void)!fread(buffer, 1, 39999, fp); // already nul filled, error checking on next line
    if (ferror(fp)) {
        perror("Error reading file");
        return 1;
    }
    fclose(fp);

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

    const int width = termpaint_surface_width(surface);
    const int height = termpaint_surface_height(surface);
    termpaint_text_measurement *m = termpaint_text_measurement_new(surface);
    termpaint_attr *attr = termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    int y = 0;
    const char* cur = buffer;
    do {
        const char* linebreak = strchr(cur, '\n');
        int count;
        if (!linebreak) {
            count = strlen(cur);
        } else {
            count = linebreak - cur;
        }
        termpaint_text_measurement_reset(m);
        termpaint_text_measurement_set_limit_width(m, width);
        termpaint_text_measurement_feed_utf8(m, cur, count, true);
        const int bytes = termpaint_text_measurement_last_ref(m);
        if (bytes != count) {
            // needs break
            // try to find a word break
            int print_bytes = bytes;
            int skip_bytes = bytes;
            for (const char *p = cur + bytes; p > cur; p--) {
                if (*p == ' ') {
                    skip_bytes = print_bytes = p - cur;
                    // the space is always a start of a cluster.
                    // when skipping the space itself after the wrap
                    // care needs to be taken to only skip the space if it is the
                    // sole character of its cluster
                    termpaint_text_measurement_reset(m);
                    termpaint_text_measurement_set_limit_clusters(m, 1);
                    termpaint_text_measurement_feed_utf8(m, cur + skip_bytes, count - skip_bytes, true);
                    if (termpaint_text_measurement_last_ref(m) == 1) {
                        skip_bytes += 1;
                    }
                    break;
                }
            }
            termpaint_surface_write_with_len_attr_clipped(surface,
                        0, y, cur, print_bytes,
                        attr,
                        0, width);
            cur += skip_bytes;
        } else {
            // fits completely
            termpaint_surface_write_with_len_attr_clipped(surface,
                        0, y, cur, count,
                        attr,
                        0, width);
            cur += count + 1;
        }
        y += 1;
        if (!linebreak) break;
    } while (y < height);

    termpaint_text_measurement_free(m);
    termpaint_attr_free(attr);

    termpaint_terminal_flush(terminal, false);

    while (!quit) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            break;
        }
    }

    termpaint_terminal_free_with_restore(terminal);

    return 0;
}
