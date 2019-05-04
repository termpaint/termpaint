#include <stdio.h>
#include <string.h>

#include "termpaint.h"
#include "termpaintx.h"

void null_callback(void *ctx, termpaint_event *event) {

}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    termpaint_integration *integration = termpaintx_full_integration_from_fd(1, 0, "+kbdsigint +kbdsigtstp");
    if (!integration) {
        puts("Could not init!");
        return 1;
    }

    termpaint_terminal *terminal = termpaint_terminal_new(integration);
    termpaint_terminal_set_event_cb(terminal, null_callback, NULL);
    termpaintx_full_integration_set_terminal(integration, terminal);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready(integration);

    char buff[1000];
    termpaint_terminal_auto_detect_result_text(terminal, buff, sizeof (buff));

    termpaint_terminal_free_with_restore(terminal);

    puts(buff);

    for (int i=1; i < argc; i++) {
        if (strcmp(argv[i], "--write-file") == 0) {
            if (i + 1 < argc) {
                FILE *f = fopen(argv[i + 1], "w");
                if (!f) {
                    perror("fopen");
                } else {
                    fputs(buff, f);
                    fclose(f);
                }
            }
        }
        if (strcmp(argv[i], "--key-wait") == 0) {
            puts("Press any key to continue");
            getchar();
        }
    }

    return 0;
}
