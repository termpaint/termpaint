#include "termpaint.h"

#include <malloc.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

/* Data model
 *
 * A surface is a 2 dimensional array of cells. A cluster occupies a continuous span of
 * 1 to 16 cells in one line.
 *
 * Each cluster can contain a string of unicode codepoints that the terminal will use
 * to form an grapheme. These strings consist of a base character and possible a
 * sequence of nonspacing combining marks. These describe the minimal unit the terminal
 * will display. If this unit is split or partially erased the whole unit will disappear.
 *
 * Additionally cluster has display (and/or semantic) attributes. These include foreground
 * and background color as well as a variety of decorations. Additionally there is a low
 * level extension mechanism to allow adding, as of now, not supported additional attributes
 * to the cluster. In the later case the application is responible for not breaking
 * rendering with these low level patches.
 *
 * Data representation
 *
 * The unicode codepoints for an cluster are either represented as a up to 8 byte utf8
 * sequence inline in the cell structure or by an reference to an separate overflow node
 * in case they do not fit within that space. These overflow nodes are managed with an
 * auxilary (per surface) hash table. Unused entries are expired when the hash table would
 * have to grow otherwise.
 *
 * Attributes consist of the following:
 * - bold (yes/no)
 * - italic (yes/no)
 * - underline (none, single, double, curly) with decoration color (default or 256 color
 *   or direct color value)
 * - blinking (yes/no)
 * - overline (yes/no)
 * - inverse (yes/no)
 * - strikethrough (yes/no)
 * - foreground color (default or 16 colors (named or bright named)
 *   or 256 color or direct color)
 * - background color (same options as foreground color)
 * - patch (an beginning and ending string of control sequences; 0 no patch else index + 1 of patches array in surface)
 *
 * text_len == 0 && text_overflow == nullptr -> same as ' '
 * text_len == 0 && text_overflow == WIDE_RIGHT_PADDING -> character hidden by multi cell cluster
 * text_len == 1 && text[0] == '\x01', only in cells_last_flush => cell was hidden, will need repaint if start of char.
 */

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

#define TERMPAINT_STYLE_PASSTHROUGH (TERMPAINT_STYLE_BOLD | TERMPAINT_STYLE_ITALIC | TERMPAINT_STYLE_BLINK \
    | TERMPAINT_STYLE_OVERLINE | TERMPAINT_STYLE_INVERSE | TERMPAINT_STYLE_STRIKE)

#define WIDE_RIGHT_PADDING ((termpaint_hash_item*)-1)

typedef struct cell_ {
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t deco_color;
    uint16_t flags; // bold, italic, underline[2], blinking, overline, inverse, strikethrough.
    uint8_t attr_patch_idx;

    uint8_t cluster_expansion : 4;
    uint8_t text_len : 4; // == 0 -> text_overflow is active or WIDE_RIGHT_PADDING.
    union {
        termpaint_hash_item* text_overflow;
        unsigned char text[8];
    };
} cell;

_Static_assert(sizeof(void*) > 8 || sizeof(cell) == 24, "bad cell size");

typedef struct termpaintp_patch_ {
    bool optimize;

    uint32_t setup_hash;
    char *setup;

    uint32_t cleanup_hash;
    char *cleanup;

    bool unused;
} termpaintp_patch;

struct termpaint_surface_ {
    bool primary;
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
    // does \033[5n work?
    AD_BASICCOMPAT,
    // Basics: cursor position, secondary id, device ready?
    AD_BASIC_REQ,
    AD_BASIC_CURPOS_RECVED,
    AD_BASIC_CURPOS_RECVED_NO_SEC_DEV_ATTRIB,
    AD_BASIC_SEC_DEV_ATTRIB_RECVED,
    // finger print 1: Test for 'private' cursor position, xterm secondary id quirk, vte CSI 1x quirk
    AD_FP1_REQ,
    AD_FP1_REQ_TERMID_RECVED,
    AD_FP1_REQ_TERMID_RECVED_SEC_DEV_ATTRIB_RECVED,
    AD_FP1_SEC_DEV_ATTRIB_RECVED,
    AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED,
    AD_FP1_QMCURSOR_POS_RECVED,
    AD_FP1_CLEANUP_AFTER_SYNC,
    AD_FP1_CLEANUP,
    AD_EXPECT_SYNC_TO_FINISH,
    AD_WAIT_FOR_SYNC_TO_FINISH,
    // finger print 2: Test for konsole repeated secondary id quirk (2 ansers), Test for VTE secondary id quirk (no answer)
    AD_FP2_REQ,
    AD_FP2_CURSOR_DONE,
    AD_FP2_SEC_DEV_ATTRIB_RECVED1,
    AD_FP2_SEC_DEV_ATTRIB_RECVED2,
} auto_detect_state;

typedef enum terminal_type_enum_ {
    TT_INCOMPATIBLE,
    TT_TOODUMB,
    TT_MISPARSING,
    TT_UNKNOWN,
    TT_BASE,
    TT_XTERM,
    TT_URXVT,
    TT_KONSOLE,
    TT_VTE,
    TT_SCREEN,
    TT_TMUX,
    TT_LINUXVC,
    TT_FULL,
} terminal_type_enum;

typedef struct termpaint_color_entry_ {
    termpaint_hash_item base;
    char *saved;
    char *requested;
    bool dirty;
    bool save_initiated;
    struct termpaint_color_entry_ *next_dirty;
} termpaint_color_entry;

typedef struct termpaint_terminal_ {
    termpaint_integration *integration;
    termpaint_surface primary;
    termpaint_input *input;
    bool data_pending_after_input_received : 1;
    bool request_repaint : 1;
    int support_qm_cursor_position_report : 1;
    int support_parsing_csi_gt_sequences : 1;
    int support_parsing_csi_equals_sequences : 1;
    char *auto_detect_sec_device_attributes;

    int terminal_type;
    int terminal_version;
    int terminal_type_confidence;
    void (*event_cb)(void *, termpaint_event *);
    void *event_user_data;
    bool (*raw_input_filter_cb)(void *user_data, const char *data, unsigned length, bool overflow);
    void *raw_input_filter_user_data;

    int initial_cursor_x;
    int initial_cursor_y;

    bool cursor_visible;
    int cursor_x;
    int cursor_y;
    int cursor_style;
    bool cursor_blink;

    int cursor_prev_data;

    termpaint_hash colors;
    termpaint_color_entry *colors_dirty;

    char *restore_seq;
    auto_detect_state ad_state;
} termpaint_terminal;

typedef enum termpaint_text_measurement_state_ {
    TM_INITIAL,
    TM_IN_CLUSTER
} termpaint_text_measurement_state;

typedef enum termpaint_text_measurement_decoder_state_ {
    TMD_INITIAL,
    TMD_PARTIAL_UTF16,
    TMD_PARTIAL_UTF8
} termpaint_text_measurement_decoder_state;

struct termpaint_text_measurement_ {
    int pending_codepoints;
    int pending_clusters;
    int pending_width;
    int pending_ref;
    int last_codepoints;
    int last_clusters;
    int last_width;
    int last_ref;
    termpaint_text_measurement_state state;

    int limit_codepoints;
    int limit_clusters;
    int limit_width;
    int limit_ref;

    // utf decoder state
    termpaint_text_measurement_decoder_state decoder_state;
    uint16_t utf_16_high;
    uint8_t utf8_size;
    uint8_t utf8_available;
    uint8_t utf8_units[6];
};

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
    attr.deco_color = TERMPAINT_DEFAULT_COLOR;
    attr.flags = 0;
    attr.patch_setup = nullptr;
    attr.patch_cleanup = nullptr;
    termpaint_surface_write_with_attr_clipped(surface, x, y, string, &attr, clip_x0, clip_x1);
}

void termpaint_surface_write_with_attr(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr) {
    termpaint_surface_write_with_attr_clipped(surface, x, y, string, attr, 0, surface->width-1);
}

// This ensures that cells [x, x + cluster_width) have cluster_expansion = 0
static void termpaintp_surface_vanish_char(termpaint_surface *surface, int x, int y, int cluster_width) {
    cell *cell = termpaintp_getcell(surface, x, y);

    int rightmost_vanished = x;

    if (cell->text_len == 0 && cell->text_overflow == WIDE_RIGHT_PADDING) {
        int i = x;
        while (cell && (cell->text_len == 0 && cell->text_overflow == WIDE_RIGHT_PADDING)) {
            cell->text_len = 1;
            cell->text[0] = ' ';
            rightmost_vanished = i;
            // cell->cluster_expansion == 0 already because padding cell

            ++i;
            cell = termpaintp_getcell(surface, i, y);
        }

        i = x - 1;
        do {
            cell = termpaintp_getcell(surface, i, y);

            if (!cell) {
                break;
            }

            cell->text_len = 1;
            cell->text[0] = ' ';
            // cell->cluster_expansion == 0 already unless this is the last iteration, see fixup below
            --i;
        } while (cell->cluster_expansion == 0);
        cell->cluster_expansion = 0;
    }

    for (int i = rightmost_vanished; i <= x + cluster_width - 1; i++) {
        cell = termpaintp_getcell(surface, i, y);

        if (!cell) {
            break;
        }

        int expansion = cell->cluster_expansion;
        int j = 0;
        while (1) {
            cell->cluster_expansion = 0;
            cell->text_len = 1;
            cell->text[0] = ' ';
            ++j;
            if (j > expansion) {
                break;
            }
            cell = termpaintp_getcell(surface, i + j, y);

            if (!cell) {
                break;
            }
        }
        i += j;
    }
}

static void termpaintp_surface_attr_apply(termpaint_surface *surface, cell *cell, termpaint_attr const *attr) {
    cell->fg_color = attr->fg_color;
    cell->bg_color = attr->bg_color;
    cell->deco_color = attr->deco_color;
    cell->flags = attr->flags;
    cell->attr_patch_idx = termpaintp_surface_ensure_patch_idx(surface, attr->patch_optimize,
                                                               attr->patch_setup, attr->patch_cleanup);
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
        int cluster_width = 1;
        int input_bytes_used = 0;
        int output_bytes_used = 0;

        // ATTENTION keep this in sync with termpaint_text_measurement_feed_codepoint
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
                } else {
                    cluster_width = width;
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

        if (cluster_width == 2 && x + 1 == clip_x0) {
            // char is split by clipping boundary. Fill in right half as if the char was split later
            cell *c = termpaintp_getcell(surface, x + 1, y);
            c->cluster_expansion = 0;

            termpaintp_surface_vanish_char(surface, x + 1, y, cluster_width - 1);

            termpaintp_surface_attr_apply(surface, c, attr);

            c->text[0] = ' ';
            c->text_len = 1;
        } else if (x + cluster_width - 1 > clip_x1) {
            // char is split by clipping boundary. Fill in left half as if the char was split later
            cell *c = termpaintp_getcell(surface, x, y);
            c->cluster_expansion = 0;

            termpaintp_surface_vanish_char(surface, x, y, cluster_width - 1);

            termpaintp_surface_attr_apply(surface, c, attr);

            c->text[0] = ' ';
            c->text_len = 1;
        } else if (x >= clip_x0) {
            cell *c = termpaintp_getcell(surface, x, y);

            termpaintp_surface_vanish_char(surface, x, y, cluster_width);

            termpaintp_surface_attr_apply(surface, c, attr);

            c->cluster_expansion = cluster_width - 1;
            if (output_bytes_used <= 8) {
                memcpy(c->text, cluster_utf8, output_bytes_used);
                c->text_len = output_bytes_used;
            } else {
                cluster_utf8[output_bytes_used] = 0;
                c->text_len = 0;
                c->text_overflow = termpaintp_hash_ensure(&surface->overflow_text, cluster_utf8);
            }
            for (int i = 1; i < cluster_width; i++) {
                cell *c = termpaintp_getcell(surface, x + i, y);
                termpaintp_surface_attr_apply(surface, c, attr);
                c->cluster_expansion = 0;
                c->text_len = 0;
                c->text_overflow = WIDE_RIGHT_PADDING;
            }
        }
        string += input_bytes_used;

        x = x + cluster_width;
    }
}

void termpaint_surface_clear_with_attr(termpaint_surface *surface, const termpaint_attr *attr) {
    termpaint_surface_clear_rect_with_attr(surface, 0, 0, surface->width, surface->height, attr);
}

void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg) {
    termpaint_surface_clear_rect(surface, 0, 0, surface->width, surface->height, fg, bg);
}

void termpaint_surface_clear_rect_with_attr(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr) {
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
        termpaintp_surface_vanish_char(surface, x, y1, 1);
        termpaintp_surface_vanish_char(surface, x + width - 1, y1, 1);
        for (int x1 = x; x1 < x + width; x1++) {
            cell* c = termpaintp_getcell(surface, x1, y1);
            c->cluster_expansion = 0;
            c->text_len = 1;
            c->text[0] = ' ';
            c->bg_color = attr->bg_color;
            c->fg_color = attr->fg_color;
            c->deco_color = TERMPAINT_DEFAULT_COLOR;
            c->flags = attr->flags;
            c->attr_patch_idx = 0;
        }
    }
}

void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg) {
    termpaint_attr attr;
    attr.fg_color = fg;
    attr.bg_color = bg;
    attr.deco_color = TERMPAINT_DEFAULT_COLOR;
    attr.flags = 0;
    attr.patch_setup = nullptr;
    attr.patch_cleanup = nullptr;
    termpaint_surface_clear_rect_with_attr(surface, x, y, width, height, &attr);
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

int termpaint_surface_width(const termpaint_surface *surface) {
    return surface->width;
}

int termpaint_surface_height(const termpaint_surface *surface) {
    return surface->height;
}

static void termpaintp_surface_gc_mark_cb(termpaint_hash *hash) {
    termpaint_surface *surface = container_of(hash, termpaint_surface, overflow_text);

    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < surface->width; x++) {
            cell* c = termpaintp_getcell(surface, x, y);
            if (c) {
                if (c->text_len == 0 && c->text_overflow != nullptr && c->text_overflow != WIDE_RIGHT_PADDING) {
                    c->text_overflow->unused = false;
                }
                if (surface->cells_last_flush) {
                    cell* old_c = &surface->cells_last_flush[y*surface->width+x];
                    if (old_c->text_len == 0 && old_c->text_overflow != nullptr && c->text_overflow != WIDE_RIGHT_PADDING) {
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

termpaint_surface *termpaint_terminal_new_surface(termpaint_terminal *term, int width, int height) {
    termpaint_surface *ret = calloc(1, sizeof(termpaint_surface));
    termpaintp_surface_init(ret);
    termpaintp_collapse(ret);
    termpaintp_resize(ret, width, height);
    return ret;
}

void termpaint_surface_free(termpaint_surface *surface) {
    // guard against freeing the primary surface
    if (surface->primary) {
        return;
    }
    termpaintp_surface_destroy(surface);
    free(surface);
}

static void termpaintp_copy_colors_and_attibutes(termpaint_surface *src_surface, cell *src_cell,
                                                 termpaint_surface *dst_surface, cell *dst_cell) {
    dst_cell->fg_color = src_cell->fg_color;
    dst_cell->bg_color = src_cell->bg_color;
    dst_cell->deco_color = src_cell->deco_color;
    dst_cell->flags = src_cell->flags;
    if (src_cell->attr_patch_idx) {
        termpaintp_patch* patch = &src_surface->patches[src_cell->attr_patch_idx - 1];
        dst_cell->attr_patch_idx = termpaintp_surface_ensure_patch_idx(dst_surface,
                                                                       patch->optimize,
                                                                       patch->setup,
                                                                       patch->cleanup);
    }
}

void termpaint_surface_tint(termpaint_surface *surface,
                            void (*recolor)(void *user_data, unsigned *fg, unsigned *bg, unsigned *deco),
                            void *user_data) {
    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < surface->width; x++) {
            cell *cell = termpaintp_getcell(surface, x, y);
            // Don't give out pointers to internal cell structure contents.
            unsigned fg = cell->fg_color;
            unsigned bg = cell->bg_color;
            unsigned deco = cell->deco_color;

            recolor(user_data, &fg, &bg, &deco);

            int expansion = cell->cluster_expansion;

            // update cluster at once, different colors in one cluster are not allowed
            for (int i = 0; i <= expansion; i++) {
                cell = termpaintp_getcell(surface, x + i, y);
                cell->fg_color = fg;
                cell->bg_color = bg;
                cell->deco_color = deco;
            }
            x += expansion;
        }
    }
}

void termpaint_surface_copy_rect(termpaint_surface *src_surface, int x, int y, int width, int height,
                                 termpaint_surface *dst_surface, int dst_x, int dst_y, int tile_left, int tile_right) {
    if (x < 0) {
        width += x;
        x = 0;
        // also switch left mode to erase
        tile_left = TERMPAINT_COPY_NO_TILE;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= src_surface->width) {
        return;
    }
    if (y >= src_surface->height) {
        return;
    }
    if (x + width > src_surface->width) {
        width = src_surface->width - x;
        // also switch right mode to erase
        tile_right = TERMPAINT_COPY_NO_TILE;
    }
    if (y + height > src_surface->height) {
        height = src_surface->height - y;
    }
    if (dst_x < 0) {
        x -= dst_x;
        width += dst_x;
        dst_x = 0;
        // also switch left mode to erase
        tile_left = TERMPAINT_COPY_NO_TILE;
    }
    if (dst_y < 0) {
        y -= dst_y;
        height += dst_y;
        dst_y = 0;
    }
    if (dst_x + width >= dst_surface->width) {
        width = dst_surface->width - dst_x;
        // also switch right mode to erase
        tile_right = TERMPAINT_COPY_NO_TILE;
    }

    if (tile_right >= TERMPAINT_COPY_TILE_PUT && dst_x + width + 1 >= dst_surface->width) {
        tile_right = TERMPAINT_COPY_NO_TILE;
    }

    if (dst_y + height >= dst_surface->height) {
        height = dst_surface->height - dst_y;
    }

    if (width == 0) {
        return;
    }

    for (int yOffset = 0; yOffset < height; yOffset++) {
        bool in_complete_cluster = false;
        int xOffset = 0;

        {
            cell *src_cell = termpaintp_getcell(src_surface, x, y + yOffset);
            if (src_cell->text_len == 0 && src_cell->text_overflow == WIDE_RIGHT_PADDING) {
                if (tile_left == TERMPAINT_COPY_TILE_PRESERVE) {
                    bool skip = false;
                    for (int i = 0; i < width; i++) {
                        cell *src_scan = termpaintp_getcell(src_surface, x + i, y + yOffset);
                        cell *dst_scan = termpaintp_getcell(dst_surface, dst_x + i, dst_y + yOffset);

                        if (!(src_scan->text_len == 0 && src_scan->text_overflow == WIDE_RIGHT_PADDING)
                            && !(dst_scan->text_len == 0 && dst_scan->text_overflow == WIDE_RIGHT_PADDING)) {
                            // end of cluster in both surfaces.
                            // skip over same length cluster in src and dst.
                            xOffset = i;
                            break;
                        }

                        if (!(dst_scan->text_len == 0 && dst_scan->text_overflow == WIDE_RIGHT_PADDING)) {
                            // cluster in dst is shorter than in src or shifted. This can not be valid tiling.
                            skip = false;
                            break;
                        }
                    }
                } else if (tile_left >= TERMPAINT_COPY_TILE_PUT && x > 0 && dst_x > 0) {
                    cell *src_scan = termpaintp_getcell(src_surface, x - 1, y + yOffset);
                    cell *dst_scan = termpaintp_getcell(dst_surface, dst_x - 1, dst_y + yOffset);

                    if ((src_scan->text_len != 0 || src_scan->text_overflow != WIDE_RIGHT_PADDING)
                            && src_scan->cluster_expansion > 0
                            && src_scan->cluster_expansion <= width) {
                        in_complete_cluster = true;

                        termpaintp_surface_vanish_char(dst_surface, dst_x - 1, dst_y + yOffset, src_scan->cluster_expansion + 1);
                        termpaintp_copy_colors_and_attibutes(src_surface, src_scan,
                                                             dst_surface, dst_scan);
                        dst_scan->cluster_expansion = src_scan->cluster_expansion;
                        if (src_scan->text_len > 0) {
                            memcpy(dst_scan->text, src_scan->text, src_scan->text_len);
                            dst_scan->text_len = src_scan->text_len;
                        } else if (src_scan->text_len == 0) {
                            dst_scan->text_len = 0;
                            dst_scan->text_overflow = termpaintp_hash_ensure(&dst_surface->overflow_text, src_scan->text_overflow->text);
                        }
                    }
                }
            }

        }

        int extra_width = 0;

        for (; xOffset < width + extra_width; xOffset++) {
            cell *src_cell = termpaintp_getcell(src_surface, x + xOffset, y + yOffset);
            cell *dst_cell = termpaintp_getcell(dst_surface, dst_x + xOffset, dst_y + yOffset);

            if (src_cell->text_len == 0 && src_cell->text_overflow == nullptr) {
                src_cell->text[0] = ' ';
                src_cell->text_len = 1;
                src_cell->deco_color = TERMPAINT_DEFAULT_COLOR;
                src_cell->fg_color = TERMPAINT_DEFAULT_COLOR;
                src_cell->bg_color = TERMPAINT_DEFAULT_COLOR;
            }

            if (src_cell->text_len == 0 && src_cell->text_overflow == WIDE_RIGHT_PADDING) {
                termpaintp_surface_vanish_char(dst_surface, dst_x + xOffset, dst_y + yOffset, 1);
                termpaintp_copy_colors_and_attibutes(src_surface, src_cell,
                                                     dst_surface, dst_cell);
                if (in_complete_cluster) {
                    dst_cell->text_len = 0;
                    dst_cell->text_overflow = WIDE_RIGHT_PADDING;
                } else {
                    dst_cell->text_len = 1;
                    dst_cell->text[0] = ' ';
                }
            } else {
                if (tile_right == TERMPAINT_COPY_TILE_PRESERVE) {
                    if (src_cell->cluster_expansion && xOffset + src_cell->cluster_expansion >= width) {
                        if (src_cell->cluster_expansion == dst_cell->cluster_expansion) {
                            // same cluster length in both, preserve cluster in dst
                            break;
                        }
                    }
                }

                termpaintp_surface_vanish_char(dst_surface, dst_x + xOffset, dst_y + yOffset, src_cell->cluster_expansion + 1);
                termpaintp_copy_colors_and_attibutes(src_surface, src_cell,
                                                     dst_surface, dst_cell);
                bool vanish = false;
                if (src_cell->cluster_expansion) {
                    if (xOffset + src_cell->cluster_expansion >= width) {
                        if (tile_right >= TERMPAINT_COPY_TILE_PUT && src_cell->cluster_expansion == 1) {
                            extra_width = 1;
                            dst_cell->cluster_expansion = src_cell->cluster_expansion;
                            in_complete_cluster = true;
                        } else {
                            vanish = true;
                            in_complete_cluster = false;
                        }
                    } else {
                        dst_cell->cluster_expansion = src_cell->cluster_expansion;
                        in_complete_cluster = true;
                    }
                } else {
                    in_complete_cluster = false;
                }

                if (!vanish) {
                    if (src_cell->text_len > 0) {
                        memcpy(dst_cell->text, src_cell->text, src_cell->text_len);
                        dst_cell->text_len = src_cell->text_len;
                    } else if (src_cell->text_len == 0) {
                        dst_cell->text_len = 0;
                        dst_cell->text_overflow = termpaintp_hash_ensure(&dst_surface->overflow_text, src_cell->text_overflow->text);
                    }
                } else {
                    dst_cell->text_len = 1;
                    dst_cell->text[0] = ' ';
                }
            }
        }
    }
}

unsigned termpaint_surface_peek_fg_color(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell(surface, x, y);
    if (cell->text_len == 0 && cell->text_overflow == nullptr) {
        return TERMPAINT_DEFAULT_COLOR;
    }
    return cell->fg_color;
}

unsigned termpaint_surface_peek_bg_color(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell(surface, x, y);
    if (cell->text_len == 0 && cell->text_overflow == nullptr) {
        return TERMPAINT_DEFAULT_COLOR;
    }
    return cell->bg_color;
}

unsigned termpaint_surface_peek_deco_color(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell(surface, x, y);
    if (cell->text_len == 0 && cell->text_overflow == nullptr) {
        return TERMPAINT_DEFAULT_COLOR;
    }
    return cell->deco_color;
}

int termpaint_surface_peek_style(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell(surface, x, y);
    unsigned flags = cell->flags;
    int style = flags & TERMPAINT_STYLE_PASSTHROUGH;
    if ((flags & CELL_ATTR_UNDERLINE_MASK) == CELL_ATTR_UNDERLINE_SINGLE) {
        style |= TERMPAINT_STYLE_UNDERLINE;
    } else if ((flags & CELL_ATTR_UNDERLINE_MASK) == CELL_ATTR_UNDERLINE_DOUBLE) {
        style |= TERMPAINT_STYLE_UNDERLINE_DBL;
    } else if ((flags & CELL_ATTR_UNDERLINE_MASK) == CELL_ATTR_UNDERLINE_CURLY) {
        style |= TERMPAINT_STYLE_UNDERLINE_CURLY;
    }
    return style;
}

void termpaint_surface_peek_patch(const termpaint_surface *surface, int x, int y, const char **setup, const char **cleanup, bool *optimize) {
    cell *cell = termpaintp_getcell(surface, x, y);
    if (cell->attr_patch_idx) {
        termpaintp_patch* patch = &surface->patches[cell->attr_patch_idx - 1];
        *setup = patch->setup;
        *cleanup = patch->cleanup;
        *optimize = patch->optimize;
    } else {
        *setup = nullptr;
        *cleanup = nullptr;
        *optimize = true;
    }
}

const char *termpaint_surface_peek_text(const termpaint_surface *surface, int x, int y, int *len, int *left, int *right) {
    cell *cell = termpaintp_getcell(surface, x, y);
    while (x > 0) {
        cell = termpaintp_getcell(surface, x, y);
        if (cell->text_len != 0 || cell->text_overflow != WIDE_RIGHT_PADDING) {
            break;
        }
        --x;
    }

    if (left) {
        *left = x;
    }

    const char *text;
    if (cell->text_len > 0) {
        text = (const char*)cell->text;
        *len = cell->text_len;
    } else if (cell->text_overflow == nullptr) {
        text = " ";
        *len = 1;
    } else {
        text = (const char*)cell->text_overflow->text;
        *len = strlen(text);
    }

    if (right) {
        *right = x + cell->cluster_expansion;
    }
    return text;
}

bool termpaint_surface_same_contents(const termpaint_surface *surface1, const termpaint_surface *surface2) {
    if (surface1 == surface2) {
        return true;
    }

    if (surface1->width != surface2->width
      || surface1->height != surface2->height) {
        return false;
    }

    for (int y = 0; y < surface1->height; y++) {
        for (int x = 0; x < surface1->width; x++) {
            if (termpaint_surface_peek_fg_color(surface1, x, y)
                    != termpaint_surface_peek_fg_color(surface2, x, y)) {
                return false;
            }
            if (termpaint_surface_peek_bg_color(surface1, x, y)
                    != termpaint_surface_peek_bg_color(surface2, x, y)) {
                return false;
            }
            if (termpaint_surface_peek_deco_color(surface1, x, y)
                    != termpaint_surface_peek_deco_color(surface2, x, y)) {
                return false;
            }
            if (termpaint_surface_peek_style(surface1, x, y)
                    != termpaint_surface_peek_style(surface2, x, y)) {
                return false;
            }
            {
                const char *setup1, *setup2;
                const char *cleanup1, *cleanup2;
                bool optimize1, optimize2;
                termpaint_surface_peek_patch(surface1, x, y, &setup1, &cleanup1, &optimize1);
                termpaint_surface_peek_patch(surface2, x, y, &setup2, &cleanup2, &optimize2);

                if ((setup1 == nullptr || setup2 == nullptr || strcmp(setup1, setup2) != 0) && !(setup1 == nullptr && setup2 == nullptr)) {
                    return false;
                }
                if ((cleanup1 == nullptr || cleanup2 == nullptr || strcmp(cleanup1, cleanup2) != 0) && !(setup1 == nullptr && setup2 == nullptr)) {
                    return false;
                }
                if (optimize1 != optimize2) {
                    return false;
                }
            }
            {
                int left1, right1, len1;
                int left2, right2, len2;
                const char *text1 = termpaint_surface_peek_text(surface1, x, y, &len1, &left1, &right1);
                const char *text2 = termpaint_surface_peek_text(surface2, x, y, &len2, &left2, &right2);

                if (left1 != left2 || right1 != right2 || len1 != len2) {
                    return false;
                }
                if (memcmp(text1, text2, len1) != 0) {
                    return false;
                }
            }
        }
    }

    return true;
}


int termpaint_surface_char_width(const termpaint_surface *surface, int codepoint) {
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

void termpaint_terminal_set_cursor(termpaint_terminal *term, int x, int y) {
    termpaint_integration *integration = term->integration;
    int_puts(integration, "\e[");
    int_put_num(integration, y+1);
    int_puts(integration, ";");
    int_put_num(integration, x+1);
    int_puts(integration, "H");
}

static void termpaintp_terminal_hide_cursor(termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;
    int_puts(integration, "\033[?25l");
}

static void termpaintp_terminal_show_cursor(termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;
    int_puts(integration, "\033[?25h");
}

static void termpaintp_terminal_update_cursor_style(termpaint_terminal *term) {
    // use support_parsing_csi_gt_sequences as indication for more advanced parsing capabilities
    bool nonharmful = term->support_parsing_csi_gt_sequences;

    if (term->terminal_type == TT_VTE && term->terminal_version < 4600) {
        nonharmful = false;
    }

    if (term->cursor_style != -1 && nonharmful) {
        const char *resetSequence = "\033[0 q";
        int cmd = term->cursor_style + (term->cursor_blink ? 0 : 1);
        if (term->terminal_type == TT_XTERM && term->terminal_version < 282 && term->cursor_style == TERMPAINT_CURSOR_STYLE_BAR) {
            // xterm < 282 does not support BAR style.
            cmd = TERMPAINT_CURSOR_STYLE_BLOCK + (term->cursor_blink ? 0 : 1);
        }
        if (cmd != term->cursor_prev_data) {
            termpaint_integration *integration = term->integration;
            if (term->terminal_type == TT_KONSOLE) {
                // konsole starting at version 18.07.70 could do the CSI space q one too, but
                // we don't have the konsole version.
                if (term->cursor_style == TERMPAINT_CURSOR_STYLE_BAR) {
                    int_puts(integration, "\x1b]50;CursorShape=1;BlinkingCursorEnabled=");
                } else if (term->cursor_style == TERMPAINT_CURSOR_STYLE_UNDERLINE) {
                    int_puts(integration, "\x1b]50;CursorShape=2;BlinkingCursorEnabled=");
                } else {
                    int_puts(integration, "\x1b]50;CursorShape=0;BlinkingCursorEnabled=");
                }
                if (term->cursor_blink) {
                    int_puts(integration, "1\a");
                } else {
                    int_puts(integration, "0\a");
                }
                resetSequence = "\x1b]50;CursorShape=0;BlinkingCursorEnabled=0\a";
            } else {
                int_puts(integration, "\033[");
                int_put_num(integration, cmd);
                int_puts(integration, " q");
            }
        }
        if (term->cursor_prev_data == -1) {
            // add style reset. We don't know the original style, so just reset to terminal default.
            termpaintp_prepend_str(&term->restore_seq, resetSequence);
        }
        term->cursor_prev_data = cmd;
    }
}

static void termpaintp_input_event_callback(void *user_data, termpaint_event *event);
static bool termpaintp_input_raw_filter_callback(void *user_data, const char *data, unsigned length, _Bool overflow);

void termpaint_color_entry_destroy(termpaint_color_entry *entry) {
    free(entry->saved);
    free(entry->requested);
}

termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration) {
    termpaint_terminal *ret = calloc(1, sizeof(termpaint_terminal));
    termpaintp_surface_init(&ret->primary);
    ret->primary.primary = true;
    // start collapsed
    termpaintp_collapse(&ret->primary);
    ret->integration = integration;

    ret->cursor_visible = true;
    ret->cursor_x = -1;
    ret->cursor_y = -1;
    ret->cursor_style = -1;
    ret->cursor_blink = false;

    ret->cursor_prev_data = -1;

    ret->data_pending_after_input_received = false;
    ret->ad_state = AD_NONE;
    ret->initial_cursor_x = -1;
    ret->initial_cursor_y = -1;
    ret->support_qm_cursor_position_report = false;
    ret->support_parsing_csi_equals_sequences = false;
    ret->support_parsing_csi_gt_sequences = false;
    ret->terminal_type = TT_UNKNOWN;
    ret->terminal_type_confidence = 0;
    ret->input = termpaint_input_new();
    termpaint_input_set_event_cb(ret->input, termpaintp_input_event_callback, ret);
    termpaint_input_set_raw_filter_cb(ret->input, termpaintp_input_raw_filter_callback, ret);

    ret->colors.item_size = sizeof(termpaint_color_entry);
    ret->colors.destroy_cb = termpaint_color_entry_destroy;

    return ret;
}

void termpaint_terminal_free(termpaint_terminal *term) {
    free(term->auto_detect_sec_device_attributes);
    term->auto_detect_sec_device_attributes = 0;
    termpaintp_surface_destroy(&term->primary);
    term->integration->free(term->integration);
    termpaintp_hash_destroy(&term->colors);
}

void termpaint_terminal_free_with_restore(termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;

    termpaintp_terminal_show_cursor(term);

    if (term->restore_seq) {
        int_puts(integration, term->restore_seq);
    }
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
    termpaintp_terminal_hide_cursor(term);
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
                c->deco_color = TERMPAINT_DEFAULT_COLOR;
                c->fg_color = TERMPAINT_DEFAULT_COLOR;
                c->bg_color = TERMPAINT_DEFAULT_COLOR;
            }
            int code_units;
            bool text_changed;
            unsigned char* text;
            if (c->text_len) {
                code_units = c->text_len;
                text = c->text;
                text_changed = old_c->text_len != c->text_len || memcmp(text, old_c->text, code_units) != 0;
            } else {
                // TODO should we avoid crash here when cluster skipping failed?
                code_units = strlen((char*)c->text_overflow->text);
                text = c->text_overflow->text;
                text_changed = old_c->text_len || c->text_overflow != old_c->text_overflow;
            }

            bool needs_paint = full_repaint || c->bg_color != old_c->bg_color || c->fg_color != old_c->fg_color
                    || c->flags != old_c->flags || c->attr_patch_idx != old_c->attr_patch_idx || text_changed;

            uint32_t effective_deco_color;
            if (c->flags & CELL_ATTR_DECO_MASK) {
                effective_deco_color = c->deco_color;
                needs_paint |= effective_deco_color != old_c->deco_color;
            } else {
                effective_deco_color = TERMPAINT_DEFAULT_COLOR;
            }

            bool needs_attribute_change = c->bg_color != current_bg || c->fg_color != current_fg
                    || effective_deco_color != current_deco || c->flags != current_flags
                    || c->attr_patch_idx != current_patch_idx;

            *old_c = *c;
            for (int i = 0; i < c->cluster_expansion; i++) {
                cell* wipe_c = &term->primary.cells_last_flush[y*term->primary.width+x+i+1];
                wipe_c->text_len = 1;
                wipe_c->text[0] = '\x01'; // impossible value, filtered out earlier in output pipeline
            }

            if (!needs_paint) {
                if (current_patch_idx) {
                    int_puts(integration, term->primary.patches[current_patch_idx-1].cleanup);
                    current_patch_idx = 0;
                }

                pending_colum_move += 1 + c->cluster_expansion;
                if (speculation_buffer_state != -1) {
                    if (needs_attribute_change) {
                        // needs_attribute_change needs >= 24 chars, so repositioning will likely be cheaper (and easier to implement)
                        speculation_buffer_state = -1;
                    } else {
                        if (pending_colum_move >= pending_colum_move_digits_step) {
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
                x += c->cluster_expansion;
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
            x += c->cluster_expansion;
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

    if (term->cursor_x != -1 && term->cursor_y != -1) {
        termpaint_terminal_set_cursor(term, term->cursor_x, term->cursor_y);
    }

    termpaintp_terminal_update_cursor_style(term);

    if (term->cursor_visible) {
        termpaintp_terminal_show_cursor(term);
    }
    if (term->colors_dirty) {
        termpaint_color_entry *entry = term->colors_dirty;
        term->colors_dirty = nullptr;
        while (entry) {
            termpaint_color_entry *next = entry->next_dirty;
            entry->next_dirty = nullptr;
            if (entry->requested) {
                int_puts(integration, "\033]");
                int_puts(integration, entry->base.text);
                int_puts(integration, ";");
                int_puts(integration, entry->requested);
                int_puts(integration, "\033\\");
            } else {
                int_puts(integration, "\033]1");
                int_puts(integration, entry->base.text);
                int_puts(integration, "\033\\");
            }
            entry = next;
        }
    }
    int_flush(integration);
}

void termpaint_terminal_reset_attributes(const termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;
    int_puts(integration, "\e[0m");
}


void termpaint_terminal_set_cursor_position(termpaint_terminal *term, int x, int y) {
    term->cursor_x = x;
    term->cursor_y = y;
}

void termpaint_terminal_set_cursor_visible(termpaint_terminal *term, bool visible) {
    term->cursor_visible = visible;
}

void termpaint_terminal_set_cursor_style(termpaint_terminal *term, int style, bool blink) {
    switch (style) {
        case TERMPAINT_CURSOR_STYLE_TERM_DEFAULT:
            term->cursor_style = style;
            term->cursor_blink = true;
            break;
        case TERMPAINT_CURSOR_STYLE_BLOCK:
        case TERMPAINT_CURSOR_STYLE_UNDERLINE:
        case TERMPAINT_CURSOR_STYLE_BAR:
            term->cursor_style = style;
            term->cursor_blink = blink;
            break;
    }
}

void termpaint_terminal_set_color(termpaint_terminal *term, int color_slot, int r, int b, int g) {
    char buff[100];
    sprintf(buff, "%d", color_slot);
    termpaint_color_entry *entry = termpaintp_hash_ensure(&term->colors, buff);
    sprintf(buff, "#%02x%02x%02x", r, g, b);
    if (entry->requested && strcmp(entry->requested, buff) == 0) {
        return;
    }

    if (color_slot == TERMPAINT_COLOR_SLOT_CURSOR) {
        // even requesting a color report does not allow to restore this, so just reset.
        // TODO: needs a sensible value for saved.
        entry->saved = strdup("");
        termpaintp_prepend_str(&term->restore_seq, "\033]112\033\\");
    }

    if (!entry->save_initiated && !entry->saved) {
        termpaint_integration *integration = term->integration;
        int_puts(integration, "\033]");
        int_put_num(integration, color_slot);
        int_puts(integration, ";?\033\\");
        int_flush(integration);
        entry->save_initiated = true;
    } else {
        if (!entry->dirty) {
            entry->dirty = true;
            entry->next_dirty = term->colors_dirty;
            term->colors_dirty = entry;
        }
    }
    free(entry->requested);
    entry->requested = strdup(buff);
}

void termpaint_terminal_reset_color(termpaint_terminal *term, int color_slot) {
    char buff[100];
    sprintf(buff, "%d", color_slot);
    termpaint_color_entry *entry = termpaintp_hash_ensure(&term->colors, buff);
    if (entry->saved) {
        if (!entry->dirty) {
            entry->dirty = true;
            entry->next_dirty = term->colors_dirty;
            term->colors_dirty = entry;
        }
        free(entry->requested);
        if (color_slot != TERMPAINT_COLOR_SLOT_CURSOR) {
            entry->requested = strdup(entry->saved);
        } else {
            entry->requested = nullptr;
        }
    }
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
        if (event->type == TERMPAINT_EV_COLOR_SLOT_REPORT) {
            char buff[100];
            sprintf(buff, "%d", event->color_slot_report.slot);
            termpaint_color_entry *entry = termpaintp_hash_ensure(&term->colors, buff);
            if (!entry->saved) {
                entry->saved = strndup(event->color_slot_report.color,
                                       event->color_slot_report.length);
                termpaintp_prepend_str(&term->restore_seq, "\033\\");
                termpaintp_prepend_str(&term->restore_seq, entry->saved);
                termpaintp_prepend_str(&term->restore_seq, ";");
                termpaintp_prepend_str(&term->restore_seq, entry->base.text);
                termpaintp_prepend_str(&term->restore_seq, "\033]");
                if (entry->requested && !entry->dirty) {
                    entry->dirty = true;
                    entry->next_dirty = term->colors_dirty;
                    term->colors_dirty = entry;
                    term->request_repaint = true;
                }
            }
        }
        term->event_cb(term->event_user_data, event);
    } else {
        termpaint_terminal_auto_detect_event(term, event);
        int_flush(term->integration);
        if (term->ad_state == AD_FINISHED) {
            if (term->terminal_type == TT_VTE) {
                const char* data = term->auto_detect_sec_device_attributes;
                if (strlen(data) > 11) {
                    bool vte_gt0_54 = memcmp(data, "\033[>65;", 6) == 0;
                    bool vte_old = memcmp(data, "\033[>1;", 5) == 0;
                    if (vte_gt0_54 || vte_old) {
                        if (vte_old) {
                            data += 5;
                        } else {
                            data += 6;
                        }
                        int version = 0;
                        while ('0' <= *data && *data <= '9') {
                            version = version * 10 + *data - '0';
                            ++data;
                        }
                        if (*data == ';' && (version < 5400) == vte_old) {
                            term->terminal_version = version;
                        }
                    }
                }
            } else if (term->terminal_type == TT_XTERM) {
                const char* data = term->auto_detect_sec_device_attributes;
                if (strlen(data) > 10) {
                    while (*data != ';' && *data != 0) {
                        ++data;
                    }
                    if (*data == ';') {
                        ++data;
                        int version = 0;
                        while ('0' <= *data && *data <= '9') {
                            version = version * 10 + *data - '0';
                            ++data;
                        }
                        if (*data == ';') {
                            term->terminal_version = version;
                        }
                    }
                }
            } else if (term->terminal_type == TT_SCREEN) {
                const char* data = term->auto_detect_sec_device_attributes;
                if (strlen(data) > 10 && memcmp(data, "\033[>83;", 6) == 0) {
                    data += 6;
                    int version = 0;
                    while ('0' <= *data && *data <= '9') {
                        version = version * 10 + *data - '0';
                        ++data;
                    }
                    if (*data == ';') {
                        term->terminal_version = version;
                    }
                }
            }

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
    bool not_in_autodetect = (term->ad_state == AD_NONE || term->ad_state == AD_FINISHED);

    if (not_in_autodetect && term->request_repaint) {
        termpaint_event event;
        event.type = TERMPAINT_EV_REPAINT_REQUESTED;
        term->event_cb(term->event_user_data, &event);
        term->request_repaint = false;
    }

    if (not_in_autodetect && termpaint_input_peek_buffer_length(term->input)) {
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

const char *termpaint_terminal_peek_input_buffer(const termpaint_terminal *term) {
    return termpaint_input_peek_buffer(term->input);
}

int termpaint_terminal_peek_input_buffer_length(const termpaint_terminal *term) {
    return termpaint_input_peek_buffer_length(term->input);
}

void termpaint_terminal_expect_cursor_position_report(termpaint_terminal *term) {
    termpaint_input_expect_cursor_position_report(term->input);
}

void termpaint_terminal_expect_legacy_mouse_reports(termpaint_terminal *term, int s) {
    termpaint_input_expect_legacy_mouse_reports(term->input, s);
}

static void termpaintp_patch_misparsing(termpaint_terminal *terminal, termpaint_integration *integration,
                                        termpaint_event *event) {
    if (terminal->initial_cursor_y == event->cursor_position.y) {
        if (terminal->initial_cursor_x + 1 == event->cursor_position.x) {
            // the c in e.g. "\033[>c" was printed
            // try to hide this
            int_puts(integration, "\010 \010");
        } else if (terminal->initial_cursor_x < event->cursor_position.x) {
            // more of this sequence got printed
            // try to hide this
            int cols = event->cursor_position.x - terminal->initial_cursor_x;
            for (int i = 0; i < cols; i++) {
                int_puts(integration, "\010");
            }
            for (int i = 0; i < cols; i++) {
                int_puts(integration, " ");
            }
            for (int i = 0; i < cols; i++) {
                int_puts(integration, "\010");
            }
        } else if (event->cursor_position.x == 1) {
            // the c in e.g. "\033[>c" was printed, causing a line wrap on the last line scrolling once
            // should we check if this is acutally the last line? Terminal size might be stale or unavailable
            // though.
            int_puts(integration, "\010 \010");
        }
    } else if (terminal->initial_cursor_y + 1 == event->cursor_position.y
               && event->cursor_position.x == 1) {
        // the c in e.g. "\033[>c" was printed, causing a line wrap
        int_puts(integration, "\010 \010");
    } else if (terminal->initial_cursor_y != event->cursor_position.y) {
        // something else, just bail out.
    }
}

// known terminals where auto detections hangs: freebsd system console using vt module
// TODO add a time out and display a message to press any key to abort.
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
            termpaint_input_expect_cursor_position_report(terminal->input);
            int_puts(integration, "\033[5n");
            int_puts(integration, "\033[6n");
            int_puts(integration, "\033[>c");
            int_puts(integration, "\033[5n");
            terminal->ad_state = AD_BASICCOMPAT;
            return true;
        case AD_BASICCOMPAT:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->ad_state = AD_BASIC_REQ;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->initial_cursor_x = event->cursor_position.x;
                terminal->initial_cursor_y = event->cursor_position.y;
                terminal->terminal_type = TT_INCOMPATIBLE;
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_BASIC_REQ:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->initial_cursor_x = event->cursor_position.x;
                terminal->initial_cursor_y = event->cursor_position.y;
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
                terminal->support_parsing_csi_gt_sequences = true;
                terminal->auto_detect_sec_device_attributes = malloc(event->raw.length + 1);
                memcpy(terminal->auto_detect_sec_device_attributes, event->raw.string, event->raw.length);
                terminal->auto_detect_sec_device_attributes[event->raw.length] = 0;
                if (event->raw.length > 6 && memcmp("\033[>85;", event->raw.string, 6) == 0) {
                    // urxvt source says: first parameter is 'U' / 85 for urxvt (except for 7.[34])
                    terminal->support_parsing_csi_equals_sequences = true;
                    terminal->terminal_type = TT_URXVT;
                    terminal->terminal_type_confidence = 2;
                }
                if (event->raw.length > 6 && memcmp("\033[>83;", event->raw.string, 6) == 0) {
                    // 83 = 'S'
                    // second parameter is version as major*10000 + minor * 100 + patch
                    terminal->support_parsing_csi_equals_sequences = true;
                    terminal->terminal_type = TT_SCREEN;
                    terminal->terminal_type_confidence = 2;
                }
                if (event->raw.length > 6 && memcmp("\033[>84;", event->raw.string, 6) == 0) {
                    // 84 = 'T'
                    // no version here
                    terminal->support_parsing_csi_equals_sequences = true;
                    terminal->terminal_type = TT_TMUX;
                    terminal->terminal_type_confidence = 2;
                }

                terminal->ad_state = AD_BASIC_SEC_DEV_ATTRIB_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_PRI_DEV_ATTRIB) {
                // We never asked for primary device attributes. This means the terminal gets
                // basic parsing rules wrong.
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_EXPECT_SYNC_TO_FINISH;
                return true;
            } else if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                // check if finger printing left printed characters
                termpaint_input_expect_cursor_position_report(terminal->input);
                int_puts(integration, "\033[6n");
                terminal->ad_state = AD_BASIC_CURPOS_RECVED_NO_SEC_DEV_ATTRIB;
                return true;
            }
            break;
        case AD_BASIC_CURPOS_RECVED_NO_SEC_DEV_ATTRIB:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->terminal_type = TT_TOODUMB;
                terminal->support_parsing_csi_gt_sequences =
                        terminal->initial_cursor_x == event->cursor_position.x
                        && terminal->initial_cursor_y == event->cursor_position.y;
                if (!terminal->support_parsing_csi_gt_sequences) {
                    terminal->terminal_type = TT_MISPARSING;
                    termpaintp_patch_misparsing(terminal, integration, event);
                }
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_BASIC_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                if (terminal->terminal_type_confidence >= 2) {
                    terminal->ad_state = AD_FINISHED;
                    return false;
                }
                int_puts(integration, "\033[=c");
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
                if (terminal->terminal_type_confidence == 0) {
                    terminal->terminal_type = TT_BASE;
                }
                terminal->support_qm_cursor_position_report = false;
                // see if "\033[=c" was misparsed
                termpaint_input_expect_cursor_position_report(terminal->input);
                int_puts(integration, "\033[6n");
                terminal->ad_state = AD_FP1_CLEANUP;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_3RD_DEV_ATTRIB) {
                terminal->support_parsing_csi_equals_sequences = true;
                if (event->raw.length == 8) {
                    // Terminal implementors: DO NOT fake other terminal IDs here!
                    // any unknown id will enable all features here. Allocate a new one!
                    if (memcmp(event->raw.string, "7E565445", 8) == 0) { // ~VTE
                        terminal->terminal_type = TT_VTE;
                        terminal->terminal_type_confidence = 2;
                    } else if (memcmp(event->raw.string, "7E4C4E58", 8) == 0) { // ~LNX
                        terminal->terminal_type = TT_LINUXVC;
                        terminal->terminal_type_confidence = 2;
                    } else if (memcmp(event->raw.string, "00000000", 8) == 0) {
                        // xterm uses this since 336. But this could be something else too.
                        terminal->terminal_type = TT_XTERM;
                        terminal->terminal_type_confidence = 1;
                    } else {
                        terminal->terminal_type = TT_FULL;
                        terminal->terminal_type_confidence = 1;
                    }
                    terminal->ad_state = AD_FP1_REQ_TERMID_RECVED;
                }
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP1_SEC_DEV_ATTRIB_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->support_qm_cursor_position_report = event->cursor_position.safe;
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                    termpaintp_patch_misparsing(terminal, integration, event);
                    terminal->terminal_type = TT_BASE;
                } else {
                    terminal->support_parsing_csi_equals_sequences = true;
                    if (event->cursor_position.safe) {
                        terminal->terminal_type = TT_XTERM;
                    } else {
                        terminal->terminal_type = TT_BASE;
                    }
                }
                terminal->ad_state = AD_FP1_QMCURSOR_POS_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                if (terminal->terminal_type_confidence == 0) {
                    terminal->terminal_type = TT_BASE;
                }
                terminal->support_qm_cursor_position_report = false;
                terminal->ad_state = AD_FP1_CLEANUP_AFTER_SYNC;
                return true;
            }
            break;
        case AD_FP1_CLEANUP:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                    termpaintp_patch_misparsing(terminal, integration, event);
                } else {
                    terminal->support_parsing_csi_equals_sequences = true;
                }
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_EXPECT_SYNC_TO_FINISH:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_FP1_CLEANUP_AFTER_SYNC:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                // see if "\033[=c" was misparsed
                if (terminal->support_qm_cursor_position_report) {
                    int_puts(integration, "\033[?6n");
                } else {
                    termpaint_input_expect_cursor_position_report(terminal->input);
                    int_puts(integration, "\033[6n");
                }
                terminal->ad_state = AD_FP1_CLEANUP;
                return true;
            }
        break;
        case AD_WAIT_FOR_SYNC_TO_FINISH:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->ad_state = AD_FINISHED;
                return false;
            } else {
                if (event->type != TERMPAINT_EV_KEY && event->type != TERMPAINT_EV_CHAR) {
                    return true;
                }
            }
            break;
        case AD_FP1_REQ_TERMID_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->support_qm_cursor_position_report = false;
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP1_REQ_TERMID_RECVED_SEC_DEV_ATTRIB_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                // keep terminal_type from terminal id
                terminal->support_qm_cursor_position_report = event->cursor_position.safe;
                terminal->ad_state = AD_WAIT_FOR_SYNC_TO_FINISH;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->support_qm_cursor_position_report = false;
                terminal->ad_state = AD_EXPECT_SYNC_TO_FINISH;
                return true;
            }
            break;
        case AD_FP1_REQ_TERMID_RECVED_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->support_qm_cursor_position_report = false;
                terminal->ad_state = AD_FINISHED;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->support_qm_cursor_position_report = event->cursor_position.safe;
                terminal->ad_state = AD_WAIT_FOR_SYNC_TO_FINISH;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                // ignore
                return true;
            }
            break;
        case AD_FP1_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->support_qm_cursor_position_report = false;
                termpaint_input_expect_cursor_position_report(terminal->input);
                int_puts(integration, "\033[6n"); // detect if "\033[=c" was misparsed
                int_puts(integration, "\033[>0;1c");
                int_puts(integration, "\033[5n");
                terminal->ad_state = AD_FP2_REQ;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->support_qm_cursor_position_report = event->cursor_position.safe;
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                    termpaintp_patch_misparsing(terminal, integration, event);
                } else {
                    terminal->support_parsing_csi_equals_sequences = true;
                }
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
                terminal->ad_state = AD_FP2_CURSOR_DONE;
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
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                    termpaintp_patch_misparsing(terminal, integration, event);
                } else {
                    terminal->support_parsing_csi_equals_sequences = true;
                }
                terminal->ad_state = AD_FP2_CURSOR_DONE;
                return true;
            }
            break;
        case AD_FP2_CURSOR_DONE:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                if (terminal->terminal_type_confidence == 0) {
                    terminal->terminal_type = TT_BASE;
                }
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP2_SEC_DEV_ATTRIB_RECVED1;
                return true;
            }
            break;
        case AD_FP2_SEC_DEV_ATTRIB_RECVED1:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                if (terminal->terminal_type_confidence == 0) {
                    terminal->terminal_type = TT_BASE;
                }
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

#if 0
    int_puts(integration, "ran off autodetect: ");
    int_put_num(integration, terminal->ad_state);
    int_puts(integration, ", ");
    int_put_num(integration, event->type);
#endif

    terminal->terminal_type = TT_TOODUMB;
    terminal->ad_state = AD_FINISHED;
    return false;
}

_Bool termpaint_terminal_auto_detect(termpaint_terminal *terminal) {
    if (!terminal->event_cb) {
        // bail out, running this without an event callback risks crashing
        return false;
    }

    // reset state just to be safe
    terminal->terminal_type = TT_UNKNOWN;
    terminal->terminal_version = 0;
    terminal->terminal_type_confidence = 0;
    terminal->initial_cursor_x = -1;
    terminal->initial_cursor_y = -1;
    terminal->support_qm_cursor_position_report = false;
    terminal->support_parsing_csi_equals_sequences = false;
    terminal->support_parsing_csi_gt_sequences = false;

    termpaint_terminal_auto_detect_event(terminal, nullptr);
    int_flush(terminal->integration);
    return true;
}

enum termpaint_auto_detect_state_enum termpaint_terminal_auto_detect_state(const termpaint_terminal *terminal) {
    if (terminal->ad_state == AD_FINISHED) {
        return termpaint_auto_detect_done;
    } else if (terminal->ad_state == AD_NONE) {
        return termpaint_auto_detect_none;
    } else {
        return termpaint_auto_detect_running;
    }
}

void termpaint_terminal_auto_detect_result_text(const termpaint_terminal *terminal, char *buffer, int buffer_length) {
    const char *term_type = nullptr;
    switch (terminal->terminal_type) {
        case TT_INCOMPATIBLE:
            term_type = "incompatible with input handling";
            break;
        case TT_TOODUMB:
            term_type = "toodumb";
            break;
        case TT_MISPARSING:
            term_type = "misparsing";
            break;
        case TT_UNKNOWN:
            term_type = "unknown";
            break;
        case TT_FULL:
            term_type = "unknown full featured";
            break;
        case TT_BASE:
            term_type = "base";
            break;
        case TT_LINUXVC:
            term_type = "linux vc";
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
        case TT_SCREEN:
            term_type = "screen";
            break;
        case TT_TMUX:
            term_type = "tmux";
            break;
        case TT_URXVT:
            term_type = "urxvt";
            break;
    };
    snprintf(buffer, buffer_length, "Type: %s(%d) %s seq:%s%s", term_type, terminal->terminal_version, terminal->support_qm_cursor_position_report ? "safe-CPR" : "",
             terminal->support_parsing_csi_gt_sequences ? ">" : "",
             terminal->support_parsing_csi_equals_sequences ? "=" : "");
    buffer[buffer_length-1] = 0;
}

static bool termpaintp_has_option(const char *options, const char *name) {
    const char *p = options;
    int name_len = strlen(name);
    while (1) {
        const char *found = strstr(p, name);
        if (!found) {
            break;
        }
        if (found == options || found[-1] == ' ') {
            if (found[name_len] == 0 || found[name_len] == ' ') {
                return true;
            }
        }
        p = found + name_len;
    }
    return false;
}

void termpaint_terminal_setup_fullscreen(termpaint_terminal *terminal, int width, int height, const char *options) {
    termpaint_integration *integration = terminal->integration;

    termpaintp_prepend_str(&terminal->restore_seq, "\033[?7h");
    int_puts(integration, "\033[?7l");

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

const char* termpaint_terminal_restore_sequence(const termpaint_terminal *term) {
    return term->restore_seq ? term->restore_seq : "";
}

termpaint_attr *termpaint_attr_new(unsigned fg, unsigned bg) {
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

void termpaint_attr_set_fg(termpaint_attr *attr, unsigned fg) {
    attr->fg_color = fg;
}

void termpaint_attr_set_bg(termpaint_attr *attr, unsigned bg) {
    attr->bg_color = bg;
}

void termpaint_attr_set_deco(termpaint_attr *attr, unsigned deco_color) {
    attr->deco_color = deco_color;
}

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
    free(attr->patch_setup);
    if (setup) {
        attr->patch_setup = strdup(setup);
    } else {
        attr->patch_setup = nullptr;
    }
    free(attr->patch_cleanup);
    if (cleanup) {
        attr->patch_cleanup = strdup(cleanup);
    } else {
        attr->patch_cleanup = nullptr;
    }
}

termpaint_text_measurement *termpaint_text_measurement_new(termpaint_surface *surface) {
    termpaint_text_measurement *m = malloc(sizeof(termpaint_text_measurement));
    termpaint_text_measurement_reset(m);
    return m;
}

void termpaint_text_measurement_free(termpaint_text_measurement *m) {
    free(m);
}

void termpaint_text_measurement_reset(termpaint_text_measurement *m) {
    m->pending_codepoints = 0;
    m->pending_ref = 0;
    m->pending_clusters = 0;
    m->pending_width = 0;
    m->last_codepoints = -1;
    m->last_ref = -1;
    m->last_clusters = -1;
    m->last_width = -1;
    m->state = TM_INITIAL;

    m->limit_codepoints = -1;
    m->limit_ref = -1;
    m->limit_clusters = -1;
    m->limit_width = -1;

    m->decoder_state = TMD_INITIAL;
}


int termpaint_text_measurement_pending_ref(termpaint_text_measurement *m) {
    int result = m->pending_ref;
    if (m->decoder_state == TMD_PARTIAL_UTF16) {
        result += 1;
    } else if (m->decoder_state == TMD_PARTIAL_UTF8) {
        result += m->utf8_available;
    }
    return result;
}


int termpaint_text_measurement_last_codepoints(termpaint_text_measurement *m) {
    return m->last_codepoints;
}

int termpaint_text_measurement_last_clusters(termpaint_text_measurement *m) {
    return m->last_clusters;
}

int termpaint_text_measurement_last_width(termpaint_text_measurement *m) {
    return m->last_width;
}

int termpaint_text_measurement_last_ref(termpaint_text_measurement *m) {
    return m->last_ref;
}

int termpaint_text_measurement_limit_codepoints(termpaint_text_measurement *m) {
    return m->limit_codepoints;
}

void termpaint_text_measurement_set_limit_codepoints(termpaint_text_measurement *m, int new_value) {
    m->limit_codepoints = new_value;
}

int termpaint_text_measurement_limit_clusters(termpaint_text_measurement *m) {
    return m->limit_clusters;
}

void termpaint_text_measurement_set_limit_clusters(termpaint_text_measurement *m, int new_value) {
    m->limit_clusters = new_value;
}

int termpaint_text_measurement_limit_width(termpaint_text_measurement *m) {
    return m->limit_width;
}

void termpaint_text_measurement_set_limit_width(termpaint_text_measurement *m, int new_value) {
    m->limit_width = new_value;
}

int termpaint_text_measurement_limit_ref(termpaint_text_measurement *m) {
    return m->limit_ref;
}

void termpaint_text_measurement_set_limit_ref(termpaint_text_measurement *m, int new_value) {
    m->limit_ref = new_value;
}

static inline void termpaintp_text_measurement_commit(termpaint_text_measurement *m) {
    m->last_codepoints = m->pending_codepoints;
    m->last_clusters = m->pending_clusters;
    m->last_width = m->pending_width;
    m->last_ref = m->pending_ref;
}

// -1 if no limit reached, 1 if some limit exceeded, 0 if no limit exceeded and some limit reached
static int termpaintp_text_measurement_cmp_limits(termpaint_text_measurement *m) {
    int ret = -1;
    if (m->limit_codepoints >= 0) {
        if (m->pending_codepoints == m->limit_codepoints) {
            ret = 0;
        } else if (m->pending_codepoints > m->limit_codepoints) {
            return 1;
        }
    }
    if (m->limit_clusters >= 0) {
        if (m->pending_clusters == m->limit_clusters) {
            ret = 0;
        } else if (m->pending_clusters > m->limit_clusters) {
            return 1;
        }
    }
    if (m->limit_width >= 0) {
        if (m->pending_width == m->limit_width) {
            ret = 0;
        } else if (m->pending_width > m->limit_width) {
            return 1;
        }
    }
    if (m->limit_ref >= 0) {
        if (m->pending_ref == m->limit_ref) {
            ret = 0;
        } else if (m->pending_ref > m->limit_ref) {
            return 1;
        }
    }
    return ret;
}

static void termpaintp_text_measurement_undo(termpaint_text_measurement *m) {
    m->pending_codepoints = m->last_codepoints;
    m->pending_ref = m->last_ref;
    m->pending_clusters = m->last_clusters;
    m->pending_width = m->last_width;
    m->state = TM_IN_CLUSTER;

    m->decoder_state = TMD_INITIAL;
}

int termpaint_text_measurement_feed_codepoint(termpaint_text_measurement *m, int ch, int ref_adjust) {
    // ATTENTION keep this in sync with actual write to surface
    ch = replace_unusable_codepoints(ch);
    int width = termpaintp_char_width(ch);
    if (width == 0) {
        if (m->state == TM_INITIAL) {
            // Assume this will be input in this way into write which will supply it with U+00a0 as base.
            // We can ignore this non spaceing mark and just calculate for U+00a0 because what is needed is
            //  * adding ref_adjust to ref (to account for the non spaceing mark)
            //  * adding one codepoint to ref (also to account for the non spaceing mark)
            //  * and incrementing the cluster and width just as U+00a0 would do. While we can ignore the non spacing
            //    mark for cluster/width purposes.
            // This is exactly what happens on feeding U+00a0, so just do that instead of creating another implementation.
            return termpaint_text_measurement_feed_codepoint(m, 0xa0, ref_adjust);
        }

        ++m->pending_codepoints;
        m->pending_ref += ref_adjust;

        // accumulates into cluster. Nothing do do here.
        return 0;
    } else {
        int limit_rel = termpaintp_text_measurement_cmp_limits(m);

        if (limit_rel == 0) { // no limit exceeded, some limit hit exactly -> commit cluster and use that as best match
            termpaintp_text_measurement_commit(m);
            m->state = TM_IN_CLUSTER;
            return TERMPAINT_MEASURE_NEW_CLUSTER | TERMPAINT_MEASURE_LIMIT_REACHED;
        } else if (limit_rel < 0) { // no limit reached -> commit cluster and go on
            // advance last full cluster
            termpaintp_text_measurement_commit(m);
            m->state = TM_IN_CLUSTER;

            ++m->pending_codepoints;
            m->pending_ref += ref_adjust;

            m->pending_width += width;
            m->pending_clusters += 1;

            return TERMPAINT_MEASURE_NEW_CLUSTER;
        } else { // some limit exceeded -> return with previous cluster as best match
            termpaintp_text_measurement_undo(m);
            return TERMPAINT_MEASURE_LIMIT_REACHED;
        }
    }
}

_Bool termpaint_text_measurement_feed_utf32(termpaint_text_measurement *m, const uint32_t *chars, int length, _Bool final) {
    for (int i = 0; i < length; i++) {
        if (termpaint_text_measurement_feed_codepoint(m, (int)chars[i], 1) & TERMPAINT_MEASURE_LIMIT_REACHED) {
            return true;
        }
    }
    if (final) {
        int limit_rel = termpaintp_text_measurement_cmp_limits(m);
        if (limit_rel == 0) { // no limit exceeded, some limit hit exactly -> commit cluster and use that as best match
            termpaintp_text_measurement_commit(m);
            return true;
        } else if (limit_rel < 0) { // no limit reached -> commit cluster
            termpaintp_text_measurement_commit(m);
            return false;
        } else { // some limit exceeded -> return with previous cluster as best match
            termpaintp_text_measurement_undo(m);
            return true;
        }
    }
    return false;
}

_Bool termpaint_text_measurement_feed_utf16(termpaint_text_measurement *m, const uint16_t *code_units, int length, _Bool final) {
    if (m->decoder_state != TMD_INITIAL && m->decoder_state != TMD_PARTIAL_UTF16) {
        // This is bogus usage, but just paper over it
        m->decoder_state = TMD_INITIAL;
    }
    for (int i = 0; i < length; i++) {
        int ch = code_units[i];
        int adjust = 1;
        if (termpaintp_utf16_is_high_surrogate(code_units[i])) {
            if (m->decoder_state != TMD_INITIAL) {
                // This is bogus usage, but just paper over it
                ch = 0xFFFD;
            } else {
                m->decoder_state = TMD_PARTIAL_UTF16;
                m->utf_16_high = code_units[i];
                continue;
            }
        }
        if (termpaintp_utf16_is_low_surrogate(code_units[i])) {
            if (m->decoder_state != TMD_PARTIAL_UTF16) {
                // This is bogus usage, but just paper over it
                ch = 0xFFFD;
            } else {
                adjust = 2;
                ch = termpaintp_utf16_combine(m->utf_16_high, code_units[i]);
            }
        }
        m->decoder_state = TMD_INITIAL;

        if (termpaint_text_measurement_feed_codepoint(m, ch, adjust) & TERMPAINT_MEASURE_LIMIT_REACHED) {
            return true;
        }
    }
    if (final) {
        int limit_rel = termpaintp_text_measurement_cmp_limits(m);
        if (limit_rel == 0) { // no limit exceeded, some limit hit exactly -> commit cluster and use that as best match
            termpaintp_text_measurement_commit(m);
            return true;
        } else if (limit_rel < 0) { // no limit reached -> commit cluster
            termpaintp_text_measurement_commit(m);
            return false;
        } else { // some limit exceeded -> return with previous cluster as best match
            termpaintp_text_measurement_undo(m);
            return true;
        }
    }
    return false;
}

_Bool termpaint_text_measurement_feed_utf8(termpaint_text_measurement *m, const uint8_t *code_units, int length, _Bool final) {
    if (m->decoder_state != TMD_INITIAL && m->decoder_state != TMD_PARTIAL_UTF8) {
        // This is bogus usage, but just paper over it
        m->decoder_state = TMD_INITIAL;
    }

    for (int i = 0; i < length; i++) {
        int ch;
        int adjust = 1;

        if (m->decoder_state == TMD_INITIAL) {
            int len = termpaintp_utf8_len(code_units[i]);
            if (len == 1) {
                ch = code_units[i];
            } else {
                m->decoder_state = TMD_PARTIAL_UTF8;
                m->utf8_size = len;
                m->utf8_units[0] = code_units[i];
                m->utf8_available = 1;
                continue;
            }
        } else {
            m->utf8_units[m->utf8_available] = code_units[i];
            m->utf8_available++;
            adjust = m->utf8_available;
            if (m->utf8_available == m->utf8_size) {
                if (termpaintp_check_valid_sequence(m->utf8_units, m->utf8_size)) {
                    ch = termpaintp_utf8_decode_from_utf8(m->utf8_units, m->utf8_size);
                } else {
                    // This is bogus usage, but just paper over it
                    ch = 0xFFFD;
                }
            } else if (m->utf8_available > m->utf8_size) {
                // This is bogus usage, but just paper over it
                ch = 0xFFFD;
            } else {
                continue;
            }
            m->decoder_state = TMD_INITIAL;
        }

        if (termpaint_text_measurement_feed_codepoint(m, ch, adjust) & TERMPAINT_MEASURE_LIMIT_REACHED) {
            return true;
        }
    }
    if (final) {
        int limit_rel = termpaintp_text_measurement_cmp_limits(m);
        if (limit_rel == 0) { // no limit exceeded, some limit hit exactly -> commit cluster and use that as best match
            termpaintp_text_measurement_commit(m);
            return true;
        } else if (limit_rel < 0) { // no limit reached -> commit cluster
            termpaintp_text_measurement_commit(m);
            return false;
        } else { // some limit exceeded -> return with previous cluster as best match
            termpaintp_text_measurement_undo(m);
            return true;
        }
    }
    return false;
}
