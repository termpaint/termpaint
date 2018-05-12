#include "termpaint.h"

#include <malloc.h>
#include <string.h>
#include <stdbool.h>

#include "termpaint_compiler.h"
#include "termpaint_input.h"
#include "termpaint_utf8.h"

/* TODO and known problems:
 * What about composing code points?
 *
 *
 */

#define nullptr ((void*)0)


typedef struct cell_ {
    unsigned char text[7]; // full 31bit range plus termination
    //_Bool double_width;
    int fg_color;
    int bg_color;
} cell;

struct termpaint_surface_ {
    cell* cells;
    cell* cells_last_flush;
    int cells_allocated;
    int width;
    int height;
};

typedef enum auto_detect_state_ {
    AD_NONE,
    AD_INITIAL,
    AD_FINISHED,
    // Basics: cursor position, secondary id, device ready?
    AD_BASIC_REQ,
    AD_BASIC_CURPOS_RECVED,
    AD_BASIC_SEC_DEV_ATTRIB_RECVED,
    // finger print 1: Test for 'private' cursor position, xterm secondary id quirk
    AD_FP1_REQ,
    AD_FP1_SEC_DEV_ATTRIB_RECVED,
    AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED,
    AD_FP1_QMCURSOR_POS_RECVED,
    // finger print 2: Test for konsole repeated secondary id quirk (2 ansers), Test for VTE secondary id quirk (no answer)
    AD_FP2_REQ,
    AD_FP2_SEC_DEV_ATTRIB_RECVED1,
    AD_FP2_SEC_DEV_ATTRIB_RECVED2,
} auto_detect_state;

typedef enum terminal_type_enum_ {
    TT_TOODUMB,
    TT_UNKNOWN,
    TT_XTERM,
    TT_URXVT,
    TT_KONSOLE,
    TT_BASE,
    TT_VTE,
} terminal_type_enum;

typedef struct termpaint_terminal_ {
    termpaint_integration *integration;
    termpaint_surface primary;
    termpaint_input *input;
    bool data_pending_after_input_received : 1;
    int support_qm_cursor_position_report : 1;
    int terminal_type : 4;
    void (*event_cb)(void *, termpaint_event *);
    void *event_user_data;
    bool (*raw_input_filter_cb)(void *user_data, const char *data, unsigned length, bool overflow);
    void *raw_input_filter_user_data;

    auto_detect_state ad_state;
} termpaint_terminal;


static void termpaintp_collapse(termpaint_surface *surface) {
    surface->width = 0;
    surface->height = 0;
    surface->cells_allocated = 0;
    surface->cells = nullptr;
    surface->cells_last_flush = nullptr;
}

static void termpaintp_resize(termpaint_surface *surface, int width, int height) {
    // TODO move contents along?

    surface->width = width;
    surface->height = height;
    _Static_assert(sizeof(int) <= sizeof(size_t), "int smaller than size_t");
    int bytes;
    if (termpaint_smul_overflow(width, height, &surface->cells_allocated)
     || termpaint_smul_overflow(surface->cells_allocated, sizeof(cell), &bytes)) {
        // collapse and bail
        free(surface->cells);
        free(surface->cells_last_flush);
        termpaintp_collapse(surface);
        return;
    }
    free(surface->cells);
    free(surface->cells_last_flush);
    surface->cells_last_flush = nullptr;
    surface->cells = calloc(1, bytes);
}

static inline cell* termpaintp_getcell(termpaint_surface *surface, int x, int y) {
    int index = y*surface->width+x;
    // TODO undefined if overflow?
    if (x >= 0 && y >= 0 && index < surface->cells_allocated) {
        return &surface->cells[index];
    } else {
        return nullptr; // FIXME how to handle this?
    }
}

static void termpaintp_surface_destroy(termpaint_surface *surface) {
    free(surface->cells);
    free(surface->cells_last_flush);
    termpaintp_collapse(surface);
}

static int replace_norenderable_codepoints(int codepoint) {
    if (codepoint < 32
       || (codepoint >= 0x7f && codepoint < 0xa0)) {
        return ' ';
    } else {
        return codepoint;
    }
}

void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg) {
    termpaint_surface_write_with_colors_clipped(surface, x, y, string, fg, bg, 0, surface->width-1);
}

void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string_s, int fg, int bg, int clip_x0, int clip_x1) {
    const unsigned char *string = (const unsigned char *)string_s;
    if (y < 0) return;
    if (clip_x0 < 0) clip_x0 = 0;
    if (clip_x1 >= surface->width) {
        clip_x1 = surface->width-1;
    }
    while (*string) {
        if (x > clip_x1 || y >= surface->height) {
            return;
        }

        int size = termpaintp_utf8_len(string[0]);

        // check termpaintp_utf8_decode_from_utf8 precondition
        for (int i = 0; i < size; i++) {
            if (string[i] == 0) {
                // bogus, bail
                return;
            }
        }
        int codepoint = termpaintp_utf8_decode_from_utf8(string, size);
        codepoint = replace_norenderable_codepoints(codepoint);

        if (x >= clip_x0) {
            cell *c = termpaintp_getcell(surface, x, y);
            c->fg_color = fg;
            c->bg_color = bg;
            int written = termpaintp_encode_to_utf8(codepoint, c->text);
            c->text[written] = 0;
        }
        string += size;

        ++x;
    }
}

void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg) {
    termpaint_surface_clear_rect(surface, 0, 0, surface->width, surface->height, fg, bg);
}

void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg) {
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= surface->width) return;
    if (y >= surface->height) return;
    if (x+width > surface->width) width = surface->width - x;
    if (y+height > surface->height) height = surface->height - y;
    for (int y1 = y; y1 < y + height; y1++) {
        for (int x1 = x; x1 < x + width; x1++) {
            cell* c = termpaintp_getcell(surface, x1, y1);
            c->text[0] = ' ';
            c->text[1] = 0;
            c->bg_color = bg;
            c->fg_color = fg;
        }
    }
}

void termpaint_surface_resize(termpaint_surface *surface, int width, int height) {
    if (width < 0 || height < 0) {
        free(surface->cells);
        free(surface->cells_last_flush);
        termpaintp_collapse(surface);
    } else {
        termpaintp_resize(surface, width, height);
    }
}

int termpaint_surface_width(termpaint_surface *surface) {
    return surface->width;
}

int termpaint_surface_height(termpaint_surface *surface) {
    return surface->height;
}


static void int_puts(termpaint_integration *integration, char *str) {
    integration->write(integration, str, strlen(str));
}

static void int_write(termpaint_integration *integration, char *str, int len) {
    integration->write(integration, str, len);
}

static void int_put_num(termpaint_integration *integration, int num) {
    char buf[12];
    int len = sprintf(buf, "%d", num);
    integration->write(integration, buf, len);
}

static void int_flush(termpaint_integration *integration) {
    integration->flush(integration);
}

static void termpaintp_input_event_callback(void *user_data, termpaint_event *event);
static bool termpaintp_input_raw_filter_callback(void *user_data, const char *data, unsigned length, _Bool overflow);

termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration) {
    termpaint_terminal *ret = calloc(1, sizeof(termpaint_terminal));

    // start collapsed
    termpaintp_collapse(&ret->primary);
    ret->integration = integration;

    ret->data_pending_after_input_received = false;
    ret->ad_state = AD_NONE;
    ret->support_qm_cursor_position_report = false;
    ret->terminal_type = TT_UNKNOWN;
    ret->input = termpaint_input_new();
    termpaint_input_set_event_cb(ret->input, termpaintp_input_event_callback, ret);
    termpaint_input_set_raw_filter_cb(ret->input, termpaintp_input_raw_filter_callback, ret);

    return ret;
}

void termpaint_terminal_free(termpaint_terminal *term) {
    termpaintp_surface_destroy(&term->primary);
    term->integration->free(term->integration);
}

termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term) {
    return &term->primary;
}

void termpaint_terminal_flush(termpaint_terminal *term, bool full_repaint) {
    termpaint_integration *integration = term->integration;
    if (!term->primary.cells_last_flush) {
        full_repaint = true;
        term->primary.cells_last_flush = calloc(1, term->primary.cells_allocated * sizeof(cell));
    }
    int_puts(integration, "\e[H");
    char speculation_buffer[30];
    int speculation_buffer_state = 0; // 0 = cursor position matches current cell, -1 = force move, > 0 bytes to print instead of move
    int pending_row_move = 0;
    int pending_colum_move = 0;
    int pending_colum_move_digits = 1;
    int pending_colum_move_digits_step = 10;
    for (int y = 0; y < term->primary.height; y++) {
        speculation_buffer_state = 0;
        pending_colum_move = 0;
        pending_colum_move_digits = 1;
        pending_colum_move_digits_step = 10;

        int current_fg = -1;
        int current_bg = -1;
        for (int x = 0; x < term->primary.width; x++) {
            cell* c = termpaintp_getcell(&term->primary, x, y);
            cell* old_c = &term->primary.cells_last_flush[y*term->primary.width+x];
            if (*c->text == 0) {
                c->text[0] = ' ';
                c->text[1] = 0;
            }
            int code_units = strlen((char*)c->text);
            bool needs_paint = full_repaint || c->bg_color != old_c->bg_color || c->fg_color != old_c->fg_color
                    || (strcmp(c->text, old_c->text) != 0);
            bool needs_attribute_change = c->bg_color != current_bg || c->fg_color != current_fg;
            *old_c = *c;
            if (!needs_paint) {
                pending_colum_move += 1;
                if (speculation_buffer_state != -1) {
                    if (needs_attribute_change) {
                        // needs_attribute_change needs >= 24 chars, so repositioning will likely be cheaper (and easier to implement)
                        speculation_buffer_state = -1;
                    } else {
                        if (pending_colum_move == pending_colum_move_digits_step) {
                            pending_colum_move_digits += 1;
                            pending_colum_move_digits_step *= 10;
                        }

                        if (pending_colum_move_digits + 3 < speculation_buffer_state + code_units) {
                            // the move sequence is shorter than moving by printing chars
                            speculation_buffer_state = -1;
                        } else if (speculation_buffer_state + code_units < sizeof (speculation_buffer)) {
                            memcpy(speculation_buffer + speculation_buffer_state, (char*)c->text, code_units);
                        } else {
                            // speculation buffer to small
                            speculation_buffer_state = -1;
                        }
                    }
                }
                continue;
            } else {
                if (pending_row_move) {
                    int_puts(integration, "\r");
                    if (pending_row_move < 4) {
                        while (pending_row_move) {
                            int_puts(integration, "\n");
                            --pending_row_move;
                        }
                    } else {
                        int_puts(integration, "\e[");
                        int_put_num(integration, pending_row_move);
                        int_puts(integration, "B");
                        pending_row_move = 0;
                    }
                }
                if (pending_colum_move) {
                    if (speculation_buffer_state > 0) {
                        int_write(integration, speculation_buffer, speculation_buffer_state);
                    } else {
                        int_puts(integration, "\e[");
                        int_put_num(integration, pending_colum_move);
                        int_puts(integration, "C");
                    }
                    speculation_buffer_state = 0;
                    pending_colum_move = 0;
                    pending_colum_move_digits = 1;
                    pending_colum_move_digits_step = 10;
                }
            }

            if (needs_attribute_change) {
                int_puts(integration, "\e[");
                if ((c->bg_color & 0xff000000) == 0) {
                    int_puts(integration, "48;2;");
                    int_put_num(integration, (c->bg_color >> 16) & 0xff);
                    int_puts(integration, ";");
                    int_put_num(integration, (c->bg_color >> 8) & 0xff);
                    int_puts(integration, ";");
                    int_put_num(integration, (c->bg_color) & 0xff);
                } else {
                    int_puts(integration, "49");
                }
                if ((c->fg_color & 0xff000000) == 0) {
                    int_puts(integration, ";38;2;");
                    int_put_num(integration, (c->fg_color >> 16) & 0xff);
                    int_puts(integration, ";");
                    int_put_num(integration, (c->fg_color >> 8) & 0xff);
                    int_puts(integration, ";");
                    int_put_num(integration, (c->fg_color) & 0xff);
                } else {
                    int_puts(integration, ";39");
                }
                int_puts(integration, "m");
                current_bg = c->bg_color;
                current_fg = c->fg_color;
            }
            int_write(integration, (char*)c->text, code_units);
        }

        if (full_repaint) {
            if (y+1 < term->primary.height) {
                int_puts(integration, "\r\n");
            }
        } else {
            pending_row_move += 1;
        }
    }
    if (pending_row_move > 1) {
        --pending_row_move; // don't move after paint rect
        int_puts(integration, "\r");
        if (pending_row_move < 4) {
            while (pending_row_move) {
                int_puts(integration, "\n");
                --pending_row_move;
            }
        } else {
            int_puts(integration, "\e[");
            int_put_num(integration, pending_row_move);
            int_puts(integration, "B");
        }
    }
    if (pending_colum_move) {
        int_puts(integration, "\e[");
        int_put_num(integration, pending_colum_move);
        int_puts(integration, "C");
    }

    int_flush(integration);
}

void termpaint_terminal_reset_attributes(termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;
    int_puts(integration, "\e[0m");
}

void termpaint_terminal_set_cursor(termpaint_terminal *term, int x, int y) {
    termpaint_integration *integration = term->integration;
    int_puts(integration, "\e[");
    int_put_num(integration, y+1);
    int_puts(integration, ";");
    int_put_num(integration, x+1);
    int_puts(integration, "H");
}

static bool termpaint_terminal_auto_detect_event(termpaint_terminal *terminal, termpaint_event *event);

static bool termpaintp_input_raw_filter_callback(void *user_data, const char *data, unsigned length, _Bool overflow) {
    termpaint_terminal *term = user_data;
    if (term->ad_state == AD_NONE || term->ad_state == AD_FINISHED) {
        return term->raw_input_filter_cb(term->raw_input_filter_user_data, data, length, overflow);
    } else {
        return false;
    }
}

static void termpaintp_input_event_callback(void *user_data, termpaint_event *event) {
    termpaint_terminal *term = user_data;
    if (term->ad_state == AD_NONE || term->ad_state == AD_FINISHED) {
        term->event_cb(term->event_user_data, event);
    } else {
        termpaint_terminal_auto_detect_event(term, event);
        int_flush(term->integration);
    }
}

void termpaint_terminal_callback(termpaint_terminal *term) {
    if (term->data_pending_after_input_received) {
        term->data_pending_after_input_received = false;
        termpaint_integration *integration = term->integration;
        int_puts(integration, "\033[5n");
        int_flush(integration);
    }
}

void termpaint_terminal_set_raw_input_filter_cb(termpaint_terminal *term, bool (*cb)(void *, const char *, unsigned, bool), void *user_data) {
    term->raw_input_filter_cb = cb;
    term->raw_input_filter_user_data = user_data;
}

void termpaint_terminal_set_event_cb(termpaint_terminal *term, void (*cb)(void *, termpaint_event *), void *user_data) {
    term->event_cb = cb;
    term->event_user_data = user_data;
}

void termpaint_terminal_add_input_data(termpaint_terminal *term, const char *data, unsigned length) {
    termpaint_input_add_data(term->input, data, length);
    if ((term->ad_state == AD_NONE || term->ad_state == AD_FINISHED) && termpaint_input_peek_buffer_length(term->input)) {
        term->data_pending_after_input_received = true;
        if (term->integration->request_callback) {
            term->integration->request_callback(term->integration);
        } else {
            termpaint_terminal_callback(term);
        }
    } else {
        term->data_pending_after_input_received = false;
    }
}

const char *termpaint_terminal_peek_input_buffer(termpaint_terminal *term) {
    return termpaint_input_peek_buffer(term->input);
}

int termpaint_terminal_peek_input_buffer_length(termpaint_terminal *term) {
    return termpaint_input_peek_buffer_length(term->input);
}

static bool termpaint_terminal_auto_detect_event(termpaint_terminal *terminal, termpaint_event *event) {
    termpaint_integration *integration = terminal->integration;

    if (event == nullptr) {
        terminal->ad_state = AD_INITIAL;
    }

    switch (terminal->ad_state) {
        case AD_FINISHED:
            // should not happen
            break;
        case AD_INITIAL:
            int_puts(integration, "\033[6n");
            int_puts(integration, "\033[>c");
            int_puts(integration, "\033[5n");
            terminal->ad_state = AD_BASIC_REQ;
            return true;
        case AD_BASIC_REQ:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                // TODO save initial cursor position
                terminal->ad_state = AD_BASIC_CURPOS_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_BASIC_CURPOS_RECVED:
            if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                // TODO save data
                /* no plot */ if (event->length > 6 && memcmp("\033[>85;", event->atom_or_string, 6) == 0) {
                    // urxvt source says: first parameter is 'U' / 85 for urxvt (except for 7.[34])
                    terminal->terminal_type = TT_URXVT;
                }
                terminal->ad_state = AD_BASIC_SEC_DEV_ATTRIB_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_BASIC_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                /* no plot */ if (terminal->terminal_type == TT_URXVT) {
                    terminal->ad_state = AD_FINISHED;
                    return false;
                }
                int_puts(integration, "\033[>1c");
                int_puts(integration, "\033[?6n");
                int_puts(integration, "\033[5n");
                terminal->ad_state = AD_FP1_REQ;
                return true;
            }
            break;
        case AD_FP1_REQ:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_BASE;
                terminal->support_qm_cursor_position_report = false;
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->support_qm_cursor_position_report = true;
                terminal->terminal_type = TT_XTERM;
                terminal->ad_state = AD_FP1_QMCURSOR_POS_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP1_SEC_DEV_ATTRIB_RECVED;
                return true;
            }
            break;
        case AD_FP1_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->support_qm_cursor_position_report = false;
                int_puts(integration, "\033[>0;1c");
                int_puts(integration, "\033[5n");
                terminal->ad_state = AD_FP2_REQ;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->support_qm_cursor_position_report = true;
                terminal->ad_state = AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED;
                return true;
            }
            break;
        case AD_FP1_QMCURSOR_POS_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                int_puts(integration, "\033[>0;1c");
                int_puts(integration, "\033[5n");
                terminal->ad_state = AD_FP2_REQ;
                return true;
            }
            break;
        case AD_FP2_REQ:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_VTE;
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP2_SEC_DEV_ATTRIB_RECVED1;
                return true;
            }
            break;
        case AD_FP2_SEC_DEV_ATTRIB_RECVED1:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_BASE;
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->terminal_type = TT_KONSOLE;
                terminal->ad_state = AD_FP2_SEC_DEV_ATTRIB_RECVED2;
                return true;
            }
            break;
        case AD_FP2_SEC_DEV_ATTRIB_RECVED2:
            if (event->type == TERMPAINT_EV_KEY && event->atom_or_string == termpaint_input_i_resync()) {
                terminal->ad_state = AD_FINISHED;
                return false;
            }
    };

    // if this code runs auto detection failed by running off into the weeds
    terminal->terminal_type = TT_TOODUMB;
    terminal->ad_state = AD_FINISHED;
    return false;
}

_Bool termpaint_terminal_auto_detect(termpaint_terminal *terminal) {
    termpaint_terminal_auto_detect_event(terminal, nullptr);
    int_flush(terminal->integration);
    return true;
}

enum termpaint_auto_detect_state_enum termpaint_terminal_auto_detect_state(termpaint_terminal *terminal) {
    if (terminal->ad_state == AD_FINISHED) {
        return termpaint_auto_detect_done;
    } else if (terminal->ad_state == AD_NONE) {
        return termpaint_auto_detect_none;
    } else {
        return termpaint_auto_detect_running;
    }
}

void termpaint_terminal_auto_detect_result_text(termpaint_terminal *terminal, char *buffer, int buffer_length) {
    const char *term_type = nullptr;
    switch (terminal->terminal_type) {
        case TT_TOODUMB:
            term_type = "toodumb";
            break;
        case TT_UNKNOWN:
            term_type = "unknown";
            break;
        case TT_BASE:
            term_type = "base";
            break;
        case TT_KONSOLE:
            term_type = "konsole";
            break;
        case TT_XTERM:
            term_type = "xterm";
            break;
        case TT_VTE:
            term_type = "vte";
            break;
        case TT_URXVT:
            term_type = "urxvt";
            break;
    };
    snprintf(buffer, buffer_length, "Type: %s %s", term_type, terminal->support_qm_cursor_position_report ? "CPR?" : "");
    buffer[buffer_length-1] = 0;
}
