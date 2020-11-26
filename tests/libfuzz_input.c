// SPDX-License-Identifier: BSL-1.0
#include <stdint.h>
#include <stddef.h>

#include "../termpaint_input.h"

__attribute__((optnone))
void null_callback(void *data, termpaint_event *event) {
    // read all memory referenced
    switch (event->type) {
        case TERMPAINT_EV_INVALID_UTF8:
        case TERMPAINT_EV_CHAR:
            for (unsigned i = 0; i < event->c.length; i++) {
                (void)event->c.string[i];
            }
            (void)event->c.modifier;
            break;
        case TERMPAINT_EV_KEY:
            for (unsigned i = 0; i < event->key.length; i++) {
                (void)event->key.atom[i];
            }
            (void)event->key.modifier;
            break;
        case TERMPAINT_EV_AUTO_DETECT_FINISHED:
        case TERMPAINT_EV_OVERFLOW:
            break;
        case TERMPAINT_EV_CURSOR_POSITION:
            (void)event->cursor_position.x;
            (void)event->cursor_position.y;
            (void)event->cursor_position.safe;
            break;
        case TERMPAINT_EV_MODE_REPORT:
            (void)event->mode.kind;
            (void)event->mode.number;
            (void)event->mode.status;
            break;
        case TERMPAINT_EV_COLOR_SLOT_REPORT:
        case TERMPAINT_EV_REPAINT_REQUESTED:
        case TERMPAINT_EV_MOUSE:
            break;
        case TERMPAINT_EV_RAW_PRI_DEV_ATTRIB:
        case TERMPAINT_EV_RAW_SEC_DEV_ATTRIB:
        case TERMPAINT_EV_RAW_3RD_DEV_ATTRIB:
        case TERMPAINT_EV_RAW_DECREQTPARM:
            for (unsigned i = 0; i < event->raw.length; i++) {
                (void)event->raw.string[i];
            }
    }

}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    termpaint_input *input_ctx = termpaint_input_new();
    termpaint_input_set_event_cb(input_ctx, null_callback, 0);
    termpaint_input_add_data(input_ctx, (const char*)data, size);
    termpaint_input_free(input_ctx);
    return 0;
}
