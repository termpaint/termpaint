#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#include "../termpaint_input.h"

void null_callback(void *data, termpaint_event *event) {
}


int main() {
    char buff[10000];
    int amount = read(0, buff, 10000);

    termpaint_input *input_ctx = termpaint_input_new();
    termpaint_input_set_event_cb(input_ctx, null_callback, 0);
    termpaint_input_add_data(input_ctx, buff, amount);
    termpaint_input_free(input_ctx);
    return 0;
}
