#include "termpaint.h"

#include <malloc.h>
#include <string.h>
#include <stdbool.h>

#include "termpaint_compiler.h"
#include "termpaint_input.h"
#include "termpaint_utf8.h"
#include "termpaint_hash.h"

#include "termpaint_char_width.h"

/* TODO and known problems:
 * What about composing code points?
 *
 *
 */

#define nullptr ((void*)0)


#define container_of(ptr, type, member) ((type *)(((char*)(ptr)) - offsetof(type, member)))

struct termpaint_attr_ {
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t deco_color;

    uint16_t flags;

    bool patch_optimize;
    unsigned char* patch_setup;
    unsigned char* patch_cleanup;
};

#define CELL_ATTR_BOLD (1 << 0)
#define CELL_ATTR_ITALIC (1 << 1)
#define CELL_ATTR_UNDERLINE_MASK (3 << 2)
#define CELL_ATTR_UNDERLINE_SINGLE (1 << 2)
#define CELL_ATTR_UNDERLINE_DOUBLE (2 << 2)
#define CELL_ATTR_UNDERLINE_CURLY (3 << 2)
#define CELL_ATTR_BLINK (1 << 4)
#define CELL_ATTR_OVERLINE (1 << 5)
#define CELL_ATTR_INVERSE (1 << 6)
#define CELL_ATTR_STRIKE (1 << 7)

#define CELL_ATTR_DECO_MASK CELL_ATTR_UNDERLINE_MASK

typedef struct cell_ {
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t deco_color;
    //_Bool double_width;
    uint16_t flags; // bold, italic, underline[2], blinking, overline, inverse, strikethrough.

    uint8_t attr_patch_idx;
    uint8_t text_len : 4; // == 0 -> text_overflow is active.
    union {
        termpaint_hash_item* text_overflow;
        unsigned char text[8];
    };
} cell;

typedef struct termpaintp_patch_ {
    bool optimize;

    uint32_t setup_hash;
    char *setup;

    uint32_t cleanup_hash;
    char *cleanup;

    bool unused;
} termpaintp_patch;

struct termpaint_surface_ {
    cell* cells;
    cell* cells_last_flush;
    int cells_allocated;
    int width;
    int height;

    termpaint_hash overflow_text;
    termpaintp_patch *patches;
};

typedef enum auto_detect_state_ {
    AD_NONE,
    AD_INITIAL,
    AD_FINISHED,
    // Basics: cursor position, secondary id, device ready?
    AD_BASIC_REQ,
    AD_BASIC_CURPOS_RECVED,
    AD_BASIC_SEC_DEV_ATTRIB_RECVED,
    // finger print 1: Test for 'private' cursor position, xterm secondary id quirk, vte CSI 1x quirk
    AD_FP1_REQ,
    AD_FP1_SEC_DEV_ATTRIB_RECVED,
    AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED,
    AD_FP1_QMCURSOR_POS_RECVED,
    AD_EXPECT_SYNC_TO_FINISH,
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

    char *restore_seq;
    auto_detect_state ad_state;
} termpaint_terminal;

static void termpaintp_append_str(char **s, const char* src) {
    int s_len = 0;
    if (*s) {
        s_len = strlen(*s);
    }
    int src_len = strlen(src);
    *s = realloc(*s, s_len + src_len + 1);
    memcpy(*s + s_len, src, src_len + 1);
}

static void termpaintp_prepend_str(char **s, const char* src) {
    size_t s_len = 0;
    if (*s) {
        s_len = strlen(*s);
    }
    size_t src_len = strlen(src);
    *s = realloc(*s, s_len + src_len + 1);
    if (s_len) {
        memmove(*s + src_len, *s, s_len);
    }
    (*s)[src_len + s_len] = 0;
    memcpy(*s, src, src_len);
}


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
    termpaintp_hash_destroy(&surface->overflow_text);

    if (surface->patches) {
        for (int i = 0; i < 255; ++i) {
            free(surface->patches[i].setup);
            free(surface->patches[i].cleanup);
        }
        free(surface->patches);
    }
    termpaintp_collapse(surface);
}

static uint8_t termpaintp_surface_ensure_patch_idx(termpaint_surface *surface, bool optimize, unsigned char *setup,
                                                unsigned char *cleanup) {
    if (!setup || !cleanup) {
        return 0;
    }

    if (!surface->patches) {
        surface->patches = calloc(255, sizeof(termpaintp_patch));
    }

    uint32_t setup_hash = termpaintp_hash_fnv1a(setup);
    uint32_t cleanup_hash = termpaintp_hash_fnv1a(cleanup);

    int free_slot = -1;

    for (int i = 0; i < 255; ++i) {
        if (!surface->patches[i].setup) {
            if (free_slot == -1) {
                free_slot = i;
            }
            continue;
        }

        if (surface->patches[i].setup_hash == setup_hash
                && surface->patches[i].cleanup_hash == cleanup_hash
                && strcmp(setup, surface->patches[i].setup) == 0
                && strcmp(cleanup, surface->patches[i].cleanup) == 0) {
            return i + 1;
        }
    }

    if (free_slot == -1) {
        // try to free unused entries
        for (int i = 0; i < 255; ++i) {
            surface->patches[i].unused = true;
        }

        for (int y = 0; y < surface->height; y++) {
            for (int x = 0; x < surface->width; x++) {
                cell* c = termpaintp_getcell(surface, x, y);
                if (c) {
                    if (c->attr_patch_idx) {
                        surface->patches[c->attr_patch_idx - 1].unused = false;
                    }
                    if (surface->cells_last_flush) {
                        cell* old_c = &surface->cells_last_flush[y*surface->width+x];
                        if (old_c->attr_patch_idx) {
                            surface->patches[old_c->attr_patch_idx - 1].unused = false;
                        }
                    }
                }
            }
        }

        for (int i = 0; i < 255; ++i) {
            if (surface->patches[i].unused) {
                free(surface->patches[i].setup);
                free(surface->patches[i].cleanup);
                surface->patches[i].setup = NULL;
                surface->patches[i].cleanup = NULL;

                if (free_slot == -1) {
                    free_slot = i;
                }
            }
        }
    }

    if (free_slot != -1) {
        surface->patches[free_slot].optimize = optimize;
        surface->patches[free_slot].setup_hash = setup_hash;
        surface->patches[free_slot].cleanup_hash = cleanup_hash;
        surface->patches[free_slot].setup = strdup(setup);
        surface->patches[free_slot].cleanup = strdup(cleanup);
        return free_slot + 1;
    }

    // can't fit anymore, just ignore it.
    return 0;
}

static int replace_unusable_codepoints(int codepoint) {
    if (codepoint < 32
       || (codepoint >= 0x7f && codepoint < 0xa0)) {
        return ' ';
    } else if (codepoint == 0xad) { // soft hyphen
        // Some implementations see this as spacing others as non spacing, make sure we get something spacing.
        return '-';
    } else {
        return codepoint;
    }
}

void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg) {
    termpaint_surface_write_with_colors_clipped(surface, x, y, string, fg, bg, 0, surface->width-1);
}

void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1) {
    termpaint_attr attr;
    attr.fg_color = fg;
    attr.bg_color = bg;
    attr.deco_color = 0;
    attr.flags = 0;
    attr.patch_setup = nullptr;
    attr.patch_cleanup = nullptr;
    termpaint_surface_write_with_attr_clipped(surface, x, y, string, &attr, clip_x0, clip_x1);
}

void termpaint_surface_write_with_attr(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr) {
    termpaint_surface_write_with_attr_clipped(surface, x, y, string, attr, 0, surface->width-1);
}
void termpaint_surface_write_with_attr_clipped(termpaint_surface *surface, int x, int y, const char *string_s, termpaint_attr const *attr, int clip_x0, int clip_x1) {
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

        unsigned char cluster_utf8[40];
        int input_bytes_used = 0;
        int output_bytes_used = 0;

        while (string[input_bytes_used]) {
            int size = termpaintp_utf8_len(string[input_bytes_used]);

            // check termpaintp_utf8_decode_from_utf8 precondition
            for (int i = 0; i < size; i++) {
                if (string[i] == 0) {
                    // bogus, bail
                    return;
                }
            }
            int codepoint = termpaintp_utf8_decode_from_utf8(string + input_bytes_used, size);
            codepoint = replace_unusable_codepoints(codepoint);

            int width = termpaintp_char_width(codepoint);

            if (!output_bytes_used) {
                if (width == 0) {
                    // if start is 0 width use U+00a0 as base
                    output_bytes_used += termpaintp_encode_to_utf8(0xa0, cluster_utf8 + output_bytes_used);
                }
                output_bytes_used += termpaintp_encode_to_utf8(codepoint, cluster_utf8 + output_bytes_used);
            } else {
                if (width > 0) {
                    // don't increase input_bytes_used here because this codepoint will need to be reprocessed.
                    break;
                }
                if (output_bytes_used + 6 < sizeof (cluster_utf8)) {
                    output_bytes_used += termpaintp_encode_to_utf8(codepoint, cluster_utf8 + output_bytes_used);
                } else {
                    // just ignore further combining codepoints, likely this is way over the limit
                    // of the terminal anyway
                }
            }
            input_bytes_used += size;
        }

        if (x >= clip_x0) {
            cell *c = termpaintp_getcell(surface, x, y);
            c->fg_color = attr->fg_color;
            c->bg_color = attr->bg_color;
            c->deco_color = attr->deco_color;
            c->flags = attr->flags;
            c->attr_patch_idx = termpaintp_surface_ensure_patch_idx(surface, attr->patch_optimize,
                                                                    attr->patch_setup, attr->patch_cleanup);
            if (output_bytes_used <= 8) {
                memcpy(c->text, cluster_utf8, output_bytes_used);
                c->text_len = output_bytes_used;
            } else {
                cluster_utf8[output_bytes_used] = 0;
                c->text_len = 0;
                c->text_overflow = termpaintp_hash_ensure(&surface->overflow_text, cluster_utf8);
            }
        }
        string += input_bytes_used;

        ++x;
    }
}

void termpaint_surface_clear_with_attr(termpaint_surface *surface, const termpaint_attr *attr) {
    termpaint_surface_clear(surface, attr->fg_color, attr->bg_color);
}

void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg) {
    termpaint_surface_clear_rect(surface, 0, 0, surface->width, surface->height, fg, bg);
}

void termpaint_surface_clear_rect_with_attr(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr) {
    termpaint_surface_clear_rect(surface, x, y, width, height, attr->fg_color, attr->bg_color);
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
            c->text_len = 1;
            c->text[0] = ' ';
            c->bg_color = bg;
            c->fg_color = fg;
            c->deco_color = 0;
            c->flags = 0;
            c->attr_patch_idx = 0;
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

int termpaint_surface_char_width(termpaint_surface *surface, int codepoint) {
    UNUSED(surface);
    // require surface here to allow for future implementation that uses terminal
    // specific information from terminal detection.
    return termpaintp_char_width(codepoint);
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

static void termpaintp_surface_gc_mark_cb(termpaint_hash *hash) {
    termpaint_surface *surface = container_of(hash, termpaint_surface, overflow_text);

    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < surface->width; x++) {
            cell* c = termpaintp_getcell(surface, x, y);
            if (c) {
                if (c->text_len == 0 && c->text_overflow != nullptr) {
                    c->text_overflow->unused = false;
                }
                if (surface->cells_last_flush) {
                    cell* old_c = &surface->cells_last_flush[y*surface->width+x];
                    if (old_c->text_len == 0 && old_c->text_overflow != nullptr) {
                        old_c->text_overflow->unused = false;
                    }
                }
            }
        }
    }
}

static void termpaintp_surface_init(termpaint_surface *surface) {
    surface->overflow_text.gc_mark_cb = termpaintp_surface_gc_mark_cb;
    surface->overflow_text.item_size = sizeof(termpaint_hash_item);
}

termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration) {
    termpaint_terminal *ret = calloc(1, sizeof(termpaint_terminal));
    termpaintp_surface_init(&ret->primary);
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

void termpaint_terminal_free_with_restore(termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;

    int_puts(integration, term->restore_seq);
    int_flush(integration);

    termpaint_terminal_free(term);
}

termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term) {
    return &term->primary;
}

static inline void write_color_sgr_values(termpaint_integration *integration, uint32_t color, char *direct, char *indexed, char *sep, unsigned named, unsigned bright_named) {
    if ((color & 0xff000000) == 0) {
        int_puts(integration, direct);
        int_put_num(integration, (color >> 16) & 0xff);
        int_puts(integration, sep);
        int_put_num(integration, (color >> 8) & 0xff);
        int_puts(integration, sep);
        int_put_num(integration, (color) & 0xff);
    } else if (TERMPAINT_INDEXED_COLOR <= color && TERMPAINT_INDEXED_COLOR + 255 >= color) {
        int_puts(integration, indexed);
        int_put_num(integration, (color) & 0xff);
    } else {
        if (named) {
            if (TERMPAINT_NAMED_COLOR <= color && TERMPAINT_NAMED_COLOR + 7 >= color) {
                int_puts(integration, ";");
                int_put_num(integration, named + (color - TERMPAINT_NAMED_COLOR));
            } else if (TERMPAINT_NAMED_COLOR + 8 <= color && TERMPAINT_NAMED_COLOR + 15 >= color) {
                int_puts(integration, ";");
                int_put_num(integration, bright_named + (color - (TERMPAINT_NAMED_COLOR + 8)));
            }
        } else {
            if (TERMPAINT_NAMED_COLOR <= color && TERMPAINT_NAMED_COLOR + 15 >= color) {
                int_puts(integration, indexed);
                int_put_num(integration, (color - TERMPAINT_NAMED_COLOR));
            }
        }
    }
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

        uint32_t current_fg = -1;
        uint32_t current_bg = -1;
        uint32_t current_deco = -1;
        uint32_t current_flags = -1;
        uint32_t current_patch_idx = 0; // patch index is special because it could do anything.

        for (int x = 0; x < term->primary.width; x++) {
            cell* c = termpaintp_getcell(&term->primary, x, y);
            cell* old_c = &term->primary.cells_last_flush[y*term->primary.width+x];
            if (c->text_len == 0 && c->text_overflow == 0) {
                c->text[0] = ' ';
                c->text_len = 1;
            }
            int code_units;
            bool text_changed;
            unsigned char* text;
            if (c->text_len) {
                code_units = c->text_len;
                text = c->text;
                text_changed = old_c->text_len != c->text_len || memcmp(text, old_c->text, code_units) != 0;
            } else {
                code_units = strlen((char*)c->text_overflow->text);
                text = c->text_overflow->text;
                text_changed = old_c->text_len || c->text_overflow != old_c->text_overflow;
            }

            bool needs_paint = full_repaint || c->bg_color != old_c->bg_color || c->fg_color != old_c->fg_color
                    || c->flags != old_c->flags || c->attr_patch_idx != old_c->attr_patch_idx || text_changed;

            uint32_t effective_deco_color;
            if (c->flags & CELL_ATTR_DECO_MASK) {
                needs_paint |= c->deco_color != old_c->deco_color;
                effective_deco_color = c->deco_color;
            } else {
                effective_deco_color = TERMPAINT_DEFAULT_COLOR;
            }

            bool needs_attribute_change = c->bg_color != current_bg || c->fg_color != current_fg
                    || effective_deco_color != current_deco || c->flags != current_flags
                    || c->attr_patch_idx != current_patch_idx;

            *old_c = *c;
            if (!needs_paint) {
                if (current_patch_idx) {
                    int_puts(integration, term->primary.patches[current_patch_idx-1].cleanup);
                    current_patch_idx = 0;
                }

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
                            memcpy(speculation_buffer + speculation_buffer_state, (char*)text, code_units);
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
                int_puts(integration, "\e[0");
                write_color_sgr_values(integration, c->bg_color, ";48;2;", ";48;5;", ";", 40, 100);
                write_color_sgr_values(integration, c->fg_color, ";38;2;", ";38;5;", ";", 30, 90);
                write_color_sgr_values(integration, effective_deco_color, ";58:2:", ";58:5:", ":", 0, 0);
                if (c->flags) {
                    if (c->flags & CELL_ATTR_BOLD) {
                        int_puts(integration, ";1");
                    }
                    if (c->flags & CELL_ATTR_ITALIC) {
                        int_puts(integration, ";3");
                    }
                    uint32_t underline = c->flags & CELL_ATTR_UNDERLINE_MASK;
                    if (underline == CELL_ATTR_UNDERLINE_SINGLE) {
                        int_puts(integration, ";4");
                    } else if (underline == CELL_ATTR_UNDERLINE_DOUBLE) {
                        int_puts(integration, ";21");
                    } else if (underline == CELL_ATTR_UNDERLINE_CURLY) {
                        // TODO maybe filter this by terminal capability somewhere?
                        int_puts(integration, ";4:3");
                    }
                    if (c->flags & CELL_ATTR_BLINK) {
                        int_puts(integration, ";5");
                    }
                    if (c->flags & CELL_ATTR_OVERLINE) {
                        int_puts(integration, ";53");
                    }
                    if (c->flags & CELL_ATTR_INVERSE) {
                        int_puts(integration, ";7");
                    }
                    if (c->flags & CELL_ATTR_STRIKE) {
                        int_puts(integration, ";9");
                    }
                }
                int_puts(integration, "m");
                current_bg = c->bg_color;
                current_fg = c->fg_color;
                current_deco = effective_deco_color;
                current_flags = c->flags;

                if (current_patch_idx != c->attr_patch_idx) {
                    if (current_patch_idx) {
                        int_puts(integration, term->primary.patches[current_patch_idx-1].cleanup);
                    }
                    if (c->attr_patch_idx) {
                        int_puts(integration, term->primary.patches[c->attr_patch_idx-1].setup);
                    }
                }

                current_patch_idx = c->attr_patch_idx;
            }
            int_write(integration, (char*)text, code_units);
            if (current_patch_idx) {
                if (!term->primary.patches[c->attr_patch_idx-1].optimize) {
                    int_puts(integration, term->primary.patches[c->attr_patch_idx-1].cleanup);
                    current_patch_idx = 0;
                }
            }
        }

        if (current_patch_idx) {
            int_puts(integration, term->primary.patches[current_patch_idx-1].cleanup);
            current_patch_idx = 0;
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
        if (term->raw_input_filter_cb) {
            return term->raw_input_filter_cb(term->raw_input_filter_user_data, data, length, overflow);
        } else {
            return false;
        }
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
        if (term->ad_state == AD_FINISHED) {
            if (term->event_cb) {
                termpaint_event event;
                event.type = TERMPAINT_EV_AUTO_DETECT_FINISHED;
                term->event_cb(term->event_user_data, &event);
            }
        }
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
            } else if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_BASIC_CURPOS_RECVED:
            if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                // TODO save data
                /* no plot */ if (event->raw.length > 6 && memcmp("\033[>85;", event->raw.string, 6) == 0) {
                    // urxvt source says: first parameter is 'U' / 85 for urxvt (except for 7.[34])
                    terminal->terminal_type = TT_URXVT;
                }
                terminal->ad_state = AD_BASIC_SEC_DEV_ATTRIB_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_BASIC_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                /* no plot */ if (terminal->terminal_type == TT_URXVT) {
                    terminal->ad_state = AD_FINISHED;
                    return false;
                }
                int_puts(integration, "\033[>1c");
                int_puts(integration, "\033[?6n");
                int_puts(integration, "\033[1x");
                int_puts(integration, "\033[5n");
                terminal->ad_state = AD_FP1_REQ;
                return true;
            }
            break;
        case AD_FP1_REQ:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
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
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->terminal_type = TT_BASE;
                terminal->support_qm_cursor_position_report = false;
                terminal->ad_state = AD_EXPECT_SYNC_TO_FINISH;
                return true;
            }
            break;
        case AD_EXPECT_SYNC_TO_FINISH:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_FP1_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->support_qm_cursor_position_report = false;
                int_puts(integration, "\033[>0;1c");
                int_puts(integration, "\033[5n");
                terminal->ad_state = AD_FP2_REQ;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->support_qm_cursor_position_report = true;
                terminal->ad_state = AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                // ignore
                return true;
            }
            break;
        case AD_FP1_QMCURSOR_POS_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                // ignore
                return true;
            }
            break;
        case AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                int_puts(integration, "\033[>0;1c");
                int_puts(integration, "\033[5n");
                terminal->ad_state = AD_FP2_REQ;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM && event->raw.length == 4 && memcmp(event->raw.string, "\033[?x", 4) == 0) {
                terminal->terminal_type = TT_VTE;
                terminal->ad_state = AD_EXPECT_SYNC_TO_FINISH;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                // ignore
                return true;
            }
            break;
        case AD_FP2_REQ:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_BASE;
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP2_SEC_DEV_ATTRIB_RECVED1;
                return true;
            }
            break;
        case AD_FP2_SEC_DEV_ATTRIB_RECVED1:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
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
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
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

static bool termpaintp_has_option(const char *options, const char *name) {
    const char *p = options;
    int name_len = strlen(name);
    const char *last_possible_location = options + strlen(options) - name_len;
    while (1) {
        const char *found = strstr(p, name);
        if (!found) {
            break;
        }
        if (found == options || found[-1] == ' ') {
            if (found == last_possible_location || found[name_len] == ' ') {
                return true;
            }
        }
        p = found + name_len;
    }
    return false;

}

void termpaint_terminal_setup_fullscreen(termpaint_terminal *terminal, int width, int height, const char *options) {
    termpaint_integration *integration = terminal->integration;

    if (!termpaintp_has_option(options, "-altscreen")) {
        termpaintp_prepend_str(&terminal->restore_seq, "\033[?1049l");
        int_puts(integration, "\033[?1049h");
    }
    termpaintp_prepend_str(&terminal->restore_seq, "\033[?66l");
    int_puts(integration, "\033[?66h");
    int_puts(integration, "\033[?1036h");
    if (!termpaintp_has_option(options, "+kbdsig") && terminal->terminal_type == TT_XTERM) {
        // xterm modify other characters
        // in this keyboard event mode xterm does no longer send the traditional one byte ^C, ^Z ^\ sequences
        // that the kernel tty layer uses to raise signals.
        termpaintp_prepend_str(&terminal->restore_seq, "\033[>4m");
        int_puts(integration, "\033[>4;2m");
    }
    int_flush(integration);

    termpaint_surface_resize(&terminal->primary, width, height);
}

const char* termpaint_terminal_restore_sequence(termpaint_terminal *term) {
    return term->restore_seq ? term->restore_seq : "";
}

termpaint_attr *termpaint_attr_new(int fg, int bg) {
    termpaint_attr *attr = calloc(1, sizeof(termpaint_attr));
    attr->fg_color = fg;
    attr->bg_color = bg;
    attr->deco_color = TERMPAINT_DEFAULT_COLOR;
    return attr;
}

void termpaint_attr_free(termpaint_attr *attr) {
    free(attr->patch_setup);
    free(attr->patch_cleanup);
    free(attr);
}

termpaint_attr *termpaint_attr_clone(termpaint_attr *orig) {
    termpaint_attr *attr = calloc(1, sizeof(termpaint_attr));
    attr->fg_color = orig->fg_color;
    attr->bg_color = orig->bg_color;
    attr->deco_color = orig->deco_color;

    attr->flags = orig->flags;

    if (orig->patch_setup) {
        attr->patch_setup = strdup(orig->patch_setup);
    }
    if (orig->patch_cleanup) {
        attr->patch_cleanup = strdup(orig->patch_cleanup);
    }
    return attr;
}
void termpaint_attr_set_fg(termpaint_attr *attr, int fg) {
    attr->fg_color = fg;
}

void termpaint_attr_set_bg(termpaint_attr *attr, int bg) {
    attr->bg_color = bg;
}

void termpaint_attr_set_deco(termpaint_attr *attr, int deco_color) {
    attr->deco_color = deco_color;
}

#define TERMPAINT_STYLE_PASSTHROUGH (TERMPAINT_STYLE_BOLD | TERMPAINT_STYLE_ITALIC | TERMPAINT_STYLE_BLINK \
    | TERMPAINT_STYLE_OVERLINE | TERMPAINT_STYLE_INVERSE | TERMPAINT_STYLE_STRIKE)

void termpaint_attr_set_style(termpaint_attr *attr, int bits) {
    attr->flags |= bits & TERMPAINT_STYLE_PASSTHROUGH;
    if (bits & ~TERMPAINT_STYLE_PASSTHROUGH) {
        if (bits & TERMPAINT_STYLE_UNDERLINE) {
            attr->flags = (attr->flags & ~CELL_ATTR_UNDERLINE_MASK) | CELL_ATTR_UNDERLINE_SINGLE;
        } else if (bits & TERMPAINT_STYLE_UNDERLINE_DBL) {
            attr->flags = (attr->flags & ~CELL_ATTR_UNDERLINE_MASK) | CELL_ATTR_UNDERLINE_DOUBLE;
        } else if (bits & TERMPAINT_STYLE_UNDERLINE_CURLY) {
            attr->flags = (attr->flags & ~CELL_ATTR_UNDERLINE_MASK) | CELL_ATTR_UNDERLINE_CURLY;
        }
    }
}

void termpaint_attr_unset_style(termpaint_attr *attr, int bits) {
    attr->flags &= ~bits | ~TERMPAINT_STYLE_PASSTHROUGH;
    if (bits & ~TERMPAINT_STYLE_PASSTHROUGH) {
        if (bits & TERMPAINT_STYLE_UNDERLINE) {
            attr->flags = attr->flags & ~CELL_ATTR_UNDERLINE_MASK;
        } else if (bits & TERMPAINT_STYLE_UNDERLINE_DBL) {
            attr->flags = attr->flags & ~CELL_ATTR_UNDERLINE_MASK;
        } else if (bits & TERMPAINT_STYLE_UNDERLINE_CURLY) {
            attr->flags = attr->flags & ~CELL_ATTR_UNDERLINE_MASK;
        }
    }
}

void termpaint_attr_reset_style(termpaint_attr *attr) {
    attr->flags = 0;
}

void termpaint_attr_set_patch(termpaint_attr *attr, bool optimize, const char *setup, const char *cleanup) {
    attr->patch_optimize = optimize;
    attr->patch_setup = strdup(setup);
    attr->patch_cleanup = strdup(cleanup);
}
