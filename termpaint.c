#include "termpaint.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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

#ifndef nullptr
#define nullptr ((void*)0)
#endif

// First cast to void* in order to silence alignment warnings because the containing structure,
// ensures that the offsets work out in a way that alignment is correct.
#define container_of(ptr, type, member) ((type *)(void*)(((char*)(ptr)) - offsetof(type, member)))

#define BUG(x) abort()

typedef unsigned char uchar;

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
 *
 * Additional invariants:
 * - The colors and flags (except CELL_SOFTWRAP_MARKER) of all cells in a cluster are identical.
 */

struct termpaint_attr_ {
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t deco_color;

    uint16_t flags;

    // If no patch is active all these are 0, otherwise both setup and cleanup contain non zero pointers.
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

#define CELL_SOFTWRAP_MARKER (1 << 15)
#define CELL_ATTR_MASK ((uint16_t)(~CELL_SOFTWRAP_MARKER))

#define TERMPAINT_STYLE_PASSTHROUGH (TERMPAINT_STYLE_BOLD | TERMPAINT_STYLE_ITALIC | TERMPAINT_STYLE_BLINK \
    | TERMPAINT_STYLE_OVERLINE | TERMPAINT_STYLE_INVERSE | TERMPAINT_STYLE_STRIKE)

#define WIDE_RIGHT_PADDING ((termpaint_hash_item*)-1)

typedef struct cell_ {
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t deco_color;
    uint16_t flags; // bold, italic, underline[2], blinking, overline, inverse, strikethrough. softwrap marker
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
    unsigned char *setup;

    uint32_t cleanup_hash;
    unsigned char *cleanup;

    bool unused;
} termpaintp_patch;

struct termpaint_surface_ {
    termpaint_terminal *terminal;

    bool primary;
    cell* cells;
    cell* cells_last_flush;
    unsigned cells_allocated;
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
    AD_BASIC_REQ_FAILED_CURPOS_RECVED,
    AD_BASIC_CURPOS_RECVED_NO_SEC_DEV_ATTRIB,
    AD_BASIC_NO_SEC_DEV_ATTRIB_MISPARSING,
    AD_BASIC_SEC_DEV_ATTRIB_RECVED,
    AD_BASIC_SEC_DEV_ATTRIB_RECVED_CONSUME_CURPOS,
    // urxvt palette size detection
    AD_URXVT_88_256_REQ,
    // finger print 1: Test for 'private' cursor position, xterm secondary id quirk, vte CSI 1x quirk
    AD_FP1_REQ,
    AD_FP1_REQ_TERMID_RECVED,
    AD_FP1_REQ_TERMID_RECVED_SEC_DEV_ATTRIB_RECVED,
    AD_FP1_SEC_DEV_ATTRIB_RECVED,
    AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED,
    AD_FP1_QMCURSOR_POS_RECVED,
    AD_FP1_3RD_DEV_ATTRIB_ALIASED_TO_PRI,
    AD_FP1_CLEANUP_AFTER_SYNC,
    AD_FP1_CLEANUP,
    AD_EXPECT_SYNC_TO_FINISH,
    AD_WAIT_FOR_SYNC_TO_FINISH,
    // finger print 2: Test for konsole repeated secondary id quirk (2 ansers), Test for VTE secondary id quirk (no answer)
    AD_FP2_REQ,
    AD_FP2_CURSOR_DONE,
    AD_FP2_SEC_DEV_ATTRIB_RECVED1,
    AD_FP2_SEC_DEV_ATTRIB_RECVED2,
    // post
    AD_WAIT_FOR_SYNC_TO_SELF_REPORTING,
    AD_EXPECT_SYNC_TO_SELF_REPORTING,
    AD_SELF_REPORTING,
    // sub routine
    AD_GLITCH_PATCHING,
    // hacks
    AD_HTERM_RECOVERY1,
    AD_HTERM_RECOVERY2
} auto_detect_state;

typedef enum terminal_type_enum_ {
    TT_INCOMPATIBLE,  // does not respond to ESC 5n, or similar deal breakers
    TT_TOODUMB,
    TT_MISPARSING,    // valid sequences (bare or >) leave visual traces
    TT_UNKNOWN,
    TT_BASE,
    TT_XTERM,
    TT_URXVT,
    TT_MLTERM,
    TT_KONSOLE,
    TT_VTE,
    TT_SCREEN,
    TT_TMUX,
    TT_LINUXVC,
    TT_MACOS,
    TT_ITERM2,
    TT_TERMINOLOGY,
    TT_KITTY,
    TT_MINTTY,
    TT_MSFT_TERMINAL,
    TT_FULL,
} terminal_type_enum;

typedef struct termpaint_color_entry_ {
    termpaint_hash_item base;
    unsigned char *saved;
    unsigned char *requested;
    bool dirty;
    bool save_initiated;
    struct termpaint_color_entry_ *next_dirty;
} termpaint_color_entry;

typedef struct termpaint_str_ {
    unsigned len;
    unsigned alloc;
    unsigned char *data;
} termpaint_str;

typedef struct termpaint_unpause_snippet_ {
    termpaint_hash_item base;
    termpaint_str sequences;
} termpaint_unpause_snippet;

typedef struct termpaint_integration_private_ {
    void (*free)(struct termpaint_integration_ *integration);
    void (*write)(struct termpaint_integration_ *integration, const char *data, int length);
    void (*flush)(struct termpaint_integration_ *integration);
    _Bool (*is_bad)(struct termpaint_integration_ *integration);
    void (*request_callback)(struct termpaint_integration_ *integration);
    void (*awaiting_response)(struct termpaint_integration_ *integration);
    void (*restore_sequence_updated)(struct termpaint_integration_ *integration, const char *data, int length);
    void (*logging_func)(struct termpaint_integration_ *integration, const char *data, int length);
} termpaint_integration_private;

#define NUM_CAPABILITIES 15

typedef struct termpaint_terminal_ {
    termpaint_integration *integration;
    termpaint_integration_private *integration_vtbl;
    termpaint_surface primary;
    termpaint_input *input;
    bool force_full_repaint;
    bool data_pending_after_input_received : 1;
    bool request_repaint : 1;
    termpaint_str auto_detect_sec_device_attributes;

    int terminal_type;
    int terminal_version;
    int terminal_type_confidence;
    termpaint_str terminal_self_reported_name_version;
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

    unsigned did_terminal_push_title : 1;
    unsigned did_terminal_add_mouse_to_restore : 1;
    unsigned did_terminal_enable_mouse : 1;
    unsigned did_terminal_add_focusreporting_to_restore : 1;
    unsigned did_terminal_add_bracketedpaste_to_restore : 1;
    unsigned did_terminal_disable_wrap : 1;

    unsigned cache_should_use_truecolor : 1;

    termpaint_str unpause_basic_setup;
    termpaint_hash unpause_snippets;

    bool glitch_on_oom;

    int cursor_prev_data; // -1 -> no touched yet, restore not setup, -2 -> force resend sequence (e.g. atfer unpause)

    termpaint_hash colors;
    termpaint_color_entry *colors_dirty;

    termpaint_str restore_seq;
    auto_detect_state ad_state;
    // additional auto detect state machine temporary space
    int glitch_cursor_x;
    int glitch_cursor_y;
    bool seen_dec_terminal_param;
    auto_detect_state glitch_patching_next_state;
    // </>
    bool capabilities[NUM_CAPABILITIES];
    int max_csi_parameters;
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

static size_t ustrlen (const uchar *s) {
    return strlen((const char*)s);
}

static unsigned char *ustrdup (const uchar *s) {
    return (unsigned char*)strdup((const char*)s);
}

static int ustrcmp (const uchar *s1, const uchar *s2) {
    return strcmp((const char*)s1, (const char*)s2);
}

static bool ustr_eq (const uchar *s1, const uchar *s2) {
    return strcmp((const char*)s1, (const char*)s2) == 0;
}


static void int_debuglog_puts(termpaint_terminal *term, const char *str);

static void termpaintp_oom_log_only(termpaint_terminal *term) {
    int_debuglog_puts(term, "failed to allocate memory, output will be incomplete\n");
}

static void termpaintp_oom(termpaint_terminal *term) {
    int_debuglog_puts(term, "failed to allocate memory, aborting\n");
    abort();
}

static void termpaintp_oom_int(termpaint_integration *integration) {
    const char *msg = "failed to allocate memory, aborting\n";
    if (integration->p->logging_func) {
        integration->p->logging_func(integration, msg, strlen(msg));
    }
    abort();
}

static void termpaintp_oom_nolog(void) {
    abort();
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

static bool termpaintp_string_prefix(const unsigned char * prefix, const unsigned char *s, int len) {
    const int plen = ustrlen(prefix);
    if (plen <= len) {
        return memcmp(prefix, s, plen) == 0;
    }
    return false;
}

static bool termpaintp_char_ascii_num(char c) {
    return '0' <= c && c <= '9';
}

static char termpaintp_char_ascii_to_lower(char c) {
    if ('A' <= c && c <= 'Z') {
        return c | 0x20;
    }
    return c;
}

static bool termpaintp_mem_ascii_case_insensitive_equals(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char ch1 = termpaintp_char_ascii_to_lower(a[i]);
        char ch2 = termpaintp_char_ascii_to_lower(b[i]);
        if (ch1 == ch2) continue;
        return false;
    }
    return true;
}

static void termpaintp_str_realloc(termpaint_str *tps, unsigned len);

static void termpaintp_prepend_str(termpaint_str *tps, const unsigned char* src) {
    size_t old_len = tps->len;
    size_t src_len = ustrlen(src);
    termpaintp_str_realloc(tps, old_len + src_len);
    if (old_len) {
        memmove(tps->data + src_len, tps->data, old_len);
    }
    tps->data[src_len + old_len] = 0;
    memcpy(tps->data, src, src_len);
    tps->len = src_len + old_len;
}

static bool termpaintp_str_ends_with(const unsigned char* str, const unsigned char* postfix) {
    size_t str_len = ustrlen(str);
    size_t postfix_len = ustrlen(postfix);
    if (str_len < postfix_len) return false;
    return memcmp(str + str_len - postfix_len, postfix, postfix_len) == 0;
}

static bool termpaintp_str_preallocate(termpaint_str *tps, unsigned len) {
    if (tps->len || tps->data) {
        BUG("preallocation only valid on unused tps");
    }
    tps->alloc = len + 1;
    tps->data = malloc(tps->alloc);
    if (!tps->data) {
        tps->alloc = 0;
        return false;
    }
    tps->data[0] = 0;
    return true;
}

// make sure tps has len capacity. contents of *data is undefined after this
static void termpaintp_str_w_e(termpaint_str *tps, unsigned len) {
    if (tps->alloc <= len) {
        free(tps->data);
        tps->alloc = len + 1;
        tps->data = malloc(tps->alloc);
        if (!tps->data) {
            termpaintp_oom_nolog();
        }
    }
}

static bool termpaintp_str_w_e_mustcheck(termpaint_str *tps, unsigned len) {
    if (tps->alloc <= len) {
        unsigned char* data_new = malloc(len + 1);
        if (!data_new) {
            return false;
        }
        free(tps->data);
        tps->alloc = len + 1;
        tps->data = data_new;
    }
    return true;
}

static void termpaintp_str_realloc(termpaint_str *tps, unsigned len) {
    if (tps->alloc <= len) {
        tps->alloc = len + 1;
        tps->data = realloc(tps->data, tps->alloc);
        if (!tps->data) {
            termpaintp_oom_nolog();
        }
    }
}

static void termpaintp_str_assign_n(termpaint_str *tps, const char *s, unsigned len) {
    termpaintp_str_w_e(tps, len);
    memcpy(tps->data, s, len);
    tps->data[len] = 0;
    tps->len = len;
}

static void termpaintp_str_assign(termpaint_str *tps, const char *s) {
    unsigned len = (unsigned)strlen(s);
    termpaintp_str_w_e(tps, len);
    memcpy(tps->data, s, len + 1);
    tps->len = len;
}

static bool termpaintp_str_assign_mustcheck(termpaint_str *tps, const char *s) {
    unsigned len = (unsigned)strlen(s);
    if (!termpaintp_str_w_e_mustcheck(tps, len)) {
        return false;
    }
    memcpy(tps->data, s, len + 1);
    tps->len = len;
    return true;
}

static void termpaintp_str_destroy(termpaint_str *tps) {
    free(tps->data);
    tps->data = nullptr;
    tps->len = 0;
    tps->alloc = 0;
}

static void termpaintp_str_append_n(termpaint_str *tps, const char *s, unsigned len) {
    termpaintp_str_realloc(tps, tps->len + len);
    memcpy(tps->data + tps->len, s, len);
    tps->len = tps->len + len;
    tps->data[tps->len] = 0;
}

static void termpaintp_str_append(termpaint_str *tps, const char *s) {
    termpaintp_str_append_n(tps, s, (unsigned)strlen(s));
}

static void termpaintp_str_append_printable_n(termpaint_str *tps, const char *str, int len) {
    int input_bytes_used = 0;
    termpaintp_str_realloc(tps, tps->len + (unsigned)len);
    while (input_bytes_used < len) {
        int size = termpaintp_utf8_len((unsigned char)str[input_bytes_used]);

        // check termpaintp_utf8_decode_from_utf8 precondition
        if (input_bytes_used + size > len) {
            // bogus, bail
            return;
        }
        if (termpaintp_check_valid_sequence((const unsigned char*)str + input_bytes_used, size)) {
            int codepoint = termpaintp_utf8_decode_from_utf8((const unsigned char*)str + input_bytes_used, size);
            int new_codepoint = replace_unusable_codepoints(codepoint);
            if (codepoint == new_codepoint) {
                termpaintp_str_append_n(tps, str + input_bytes_used, size);
            } else {
                if (new_codepoint < 128) {
                    char ch;
                    ch = new_codepoint;
                    termpaintp_str_append_n(tps, &ch, 1);
                }
            }
        }
        input_bytes_used += size;
    }
}

#define TERMPAINTP_STR_STORE_S(var, value) const char *var = (value)
#define TERMPAINTP_STR_SIZER_S(value) strlen(value)
#define TERMPAINTP_STR_APPEND_S(tps, value, len) termpaintp_str_append_n((tps), (value), (len))

#define TERMPAINTP_STR_STORE_F(var, value) const char *var = (value)
#define TERMPAINTP_STR_SIZER_F(value) strlen(value)
#define TERMPAINTP_STR_APPEND_F(tps, value, len) termpaintp_str_append_printable_n((tps), (value), (len))

#define TERMPAINT_STR_ASSIGN3_MUSTCHECK(tps, type1, value1, type2, value2, type3, value3) \
    do {                                                                               \
        termpaint_str* tps_TMP_tps = (tps);                                            \
        TERMPAINTP_STR_STORE_##type1(tps_TMP_1, (value1));                             \
        TERMPAINTP_STR_STORE_##type2(tps_TMP_2, (value2));                             \
        TERMPAINTP_STR_STORE_##type3(tps_TMP_3, (value3));                             \
        unsigned tps_TMP_len1 = (unsigned)TERMPAINTP_STR_SIZER_##type1(tps_TMP_1);     \
        unsigned tps_TMP_len2 = (unsigned)TERMPAINTP_STR_SIZER_##type2(tps_TMP_2);     \
        unsigned tps_TMP_len3 = (unsigned)TERMPAINTP_STR_SIZER_##type3(tps_TMP_3);     \
        tps_TMP_tps->len = 0;                                                          \
        if (termpaintp_str_w_e_mustcheck(tps_TMP_tps, tps_TMP_len1 + tps_TMP_len2 + tps_TMP_len3)) {  \
            TERMPAINTP_STR_APPEND_##type1(tps_TMP_tps, tps_TMP_1, (tps_TMP_len1));     \
            TERMPAINTP_STR_APPEND_##type2(tps_TMP_tps, tps_TMP_2, (tps_TMP_len2));     \
            TERMPAINTP_STR_APPEND_##type3(tps_TMP_tps, tps_TMP_3, (tps_TMP_len3));     \
        }                                                                              \
    } while (0)                                                                        \
    /* end */


_tERMPAINT_PUBLIC bool termpaint_integration_init_mustcheck(termpaint_integration *integration,
                                                           void (*free)(struct termpaint_integration_ *integration),
                                                           void (*write)(struct termpaint_integration_ *integration, const char *data, int length),
                                                           void (*flush)(struct termpaint_integration_ *integration)) {
    integration->p = calloc(1, sizeof(termpaint_integration_private));
    if (!integration->p) {
        return false;
    }
    integration->p->free = free;
    integration->p->write = write;
    integration->p->flush = flush;
    return true;
}

_tERMPAINT_PUBLIC void termpaint_integration_init(termpaint_integration *integration,
                                                  void (*free)(struct termpaint_integration_ *integration),
                                                  void (*write)(struct termpaint_integration_ *integration, const char *data, int length),
                                                  void (*flush)(struct termpaint_integration_ *integration)) {
    if (!termpaint_integration_init_mustcheck(integration, free, write, flush)) {
        termpaintp_oom_nolog();
    }
}

_tERMPAINT_PUBLIC void termpaint_integration_set_is_bad(termpaint_integration *integration, _Bool (*is_bad)(struct termpaint_integration_ *integration)) {
    integration->p->is_bad = is_bad;
}

_tERMPAINT_PUBLIC void termpaint_integration_set_request_callback(termpaint_integration *integration, void (*request_callback)(struct termpaint_integration_ *integration)) {
    integration->p->request_callback = request_callback;
}

_tERMPAINT_PUBLIC void termpaint_integration_set_awaiting_response(termpaint_integration *integration, void (*awaiting_response)(struct termpaint_integration_ *integration)) {
    integration->p->awaiting_response = awaiting_response;
}

_tERMPAINT_PUBLIC void termpaint_integration_set_restore_sequence_updated(termpaint_integration *integration, void (*restore_sequence_updated)(struct termpaint_integration_ *integration, const char *data, int length)) {
    integration->p->restore_sequence_updated = restore_sequence_updated;
}

_tERMPAINT_PUBLIC void termpaint_integration_set_logging_func(termpaint_integration *integration, void (*logging_func)(termpaint_integration *integration, const char *data, int length)) {
    integration->p->logging_func = logging_func;
}

void termpaint_integration_deinit(termpaint_integration *integration) {
    free(integration->p);
    integration->p = nullptr;
}

static void termpaintp_collapse(termpaint_surface *surface) {
    surface->width = 0;
    surface->height = 0;
    surface->cells_allocated = 0;
    surface->cells = nullptr;
    surface->cells_last_flush = nullptr;
}

static bool termpaintp_resize_mustcheck(termpaint_surface *surface, int width, int height) {
    // TODO move contents along?

    surface->width = width;
    surface->height = height;
    _Static_assert(sizeof(int) <= sizeof(size_t), "int smaller than size_t");
    int bytes;
    int cell_count;
    if (
        (width < 0) || (height < 0)
     || termpaint_smul_overflow(width, height, &cell_count)
     || termpaint_smul_overflow(cell_count, sizeof(cell), &bytes)) {
        // collapse and bail
        free(surface->cells);
        free(surface->cells_last_flush);
        termpaintp_collapse(surface);
        return true; // This is debatable, but the previous code did allow this and there are tests for this.
    }
    surface->cells_allocated = cell_count;
    free(surface->cells);
    free(surface->cells_last_flush);
    surface->cells_last_flush = nullptr;
    surface->cells = calloc(1, bytes);
    if (!surface->cells) {
        termpaintp_collapse(surface);
        return false;
    }

    if (surface->primary) {
        surface->terminal->force_full_repaint = true;
        surface->cells_last_flush = calloc(1, surface->cells_allocated * sizeof(cell));
        if (!surface->cells_last_flush) {
            free(surface->cells);
            termpaintp_collapse(surface);
            return false;
        }
    }
    return true;
}

static inline cell* termpaintp_getcell(const termpaint_surface *surface, int x, int y) {
    unsigned index = y*surface->width + x;
    if (x >= 0 && y >= 0
        && x < surface->width && y < surface->height
        && index < surface->cells_allocated) {
        return &surface->cells[index];
    } else {
        BUG("cell out of range");
    }
}

static inline cell* termpaintp_getcell_or_null(const termpaint_surface *surface, int x, int y) {
    unsigned index = y*surface->width + x;
    if (x >= 0 && y >= 0
        && x < surface->width && y < surface->height) {
        if (index < surface->cells_allocated) {
            return &surface->cells[index];
        } else {
            BUG("cell out of range");
        }
    } else {
        return nullptr;
    }
}

static void termpaintp_set_overflow_text(termpaint_surface *surface, cell *dst_cell, const unsigned char* data) {
    // hash_ensure needs to be done before touching text_len, because it can cause garbage collection which would
    // see an inconistant state if text_len is already set to zero.
    void* overflow_ptr = termpaintp_hash_ensure(&surface->overflow_text, data);
    if (!overflow_ptr) {
        if (!surface->terminal->glitch_on_oom) {
            termpaintp_oom(surface->terminal);
        } else {
            termpaintp_oom_log_only(surface->terminal);
            dst_cell->text_len = 1;
            dst_cell->text[0] = '?';
        }
    }
    dst_cell->text_len = 0;
    dst_cell->text_overflow = overflow_ptr;
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
        surface->patches = nullptr;
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
        if (!surface->patches) {
            if (!surface->terminal->glitch_on_oom) {
                termpaintp_oom(surface->terminal);
            } else {
                termpaintp_oom_log_only(surface->terminal);
                return 0;
            }
        }
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
                && ustrcmp(setup, surface->patches[i].setup) == 0
                && ustrcmp(cleanup, surface->patches[i].cleanup) == 0) {
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

        for (int i = 0; i < 255; ++i) {
            if (surface->patches[i].unused) {
                free(surface->patches[i].setup);
                free(surface->patches[i].cleanup);
                surface->patches[i].setup = nullptr;
                surface->patches[i].cleanup = nullptr;

                if (free_slot == -1) {
                    free_slot = i;
                }
            }
        }
    }

    if (free_slot != -1) {
        unsigned char *setup_copy = ustrdup(setup);
        unsigned char *cleanup_copy = ustrdup(cleanup);
        if (!setup_copy || !cleanup_copy) {
            if (!surface->terminal->glitch_on_oom) {
                termpaintp_oom(surface->terminal);
            } else {
                free(setup_copy);
                free(cleanup_copy);
                termpaintp_oom_log_only(surface->terminal);
                return 0;
            }
        }
        surface->patches[free_slot].optimize = optimize;
        surface->patches[free_slot].setup_hash = setup_hash;
        surface->patches[free_slot].cleanup_hash = cleanup_hash;
        surface->patches[free_slot].setup = setup_copy;
        surface->patches[free_slot].cleanup = cleanup_copy;

        return free_slot + 1;
    }

    // can't fit anymore, just ignore it.
    return 0;
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
    attr.patch_optimize = false;
    termpaint_surface_write_with_attr_clipped(surface, x, y, string, &attr, clip_x0, clip_x1);
}

void termpaint_surface_write_with_attr(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr) {
    termpaint_surface_write_with_attr_clipped(surface, x, y, string, attr, 0, surface->width-1);
}

// This ensures that cells [x, x + cluster_width) have cluster_expansion = 0
static void termpaintp_surface_vanish_char(termpaint_surface *surface, int x, int y, int cluster_width) {
    // narrow contract, x + cluster_width <= width
    cell *cell = termpaintp_getcell(surface, x, y);

    int rightmost_vanished = x;

    if (cell->text_len == 0 && cell->text_overflow == WIDE_RIGHT_PADDING) {
        int i = x;
        while (cell->text_len == 0 && cell->text_overflow == WIDE_RIGHT_PADDING) {
            cell->text_len = 1;
            cell->text[0] = ' ';
            rightmost_vanished = i;
            // cell->cluster_expansion == 0 already because padding cell

            if (i + 1 == surface->width) {
                break;
            }

            ++i;
            cell = termpaintp_getcell(surface, i, y);
        }

        i = x - 1;
        do {
            cell = termpaintp_getcell(surface, i, y);

            cell->text_len = 1;
            cell->text[0] = ' ';
            // cell->cluster_expansion == 0 already unless this is the last iteration, see fixup below
            --i;
        } while (cell->cluster_expansion == 0);
        cell->cluster_expansion = 0;
    }

    for (int i = rightmost_vanished; i <= x + cluster_width - 1; i++) {
        cell = termpaintp_getcell(surface, i, y);

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
        size_t output_bytes_used = 0;

        // ATTENTION keep this in sync with termpaint_text_measurement_feed_codepoint
        while (string[input_bytes_used]) {
            int size = termpaintp_utf8_len(string[input_bytes_used]);

            // check termpaintp_utf8_decode_from_utf8 precondition
            for (int i = 0; i < size; i++) {
                if (string[input_bytes_used + i] == 0) {
                    // bogus, bail
                    return;
                }
            }
            int codepoint;
            if (termpaintp_check_valid_sequence(string + input_bytes_used, size)) {
                codepoint = termpaintp_utf8_decode_from_utf8(string + input_bytes_used, size);
            } else {
                // This is bogus usage, but just paper over it
                codepoint = 0xFFFD;
            }

            if (codepoint != '\x7f' || output_bytes_used != 0) {
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
            } else {
                output_bytes_used = 0;
                input_bytes_used += size;
                // do not allow any non spacing modifiers
                break;
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
                if (output_bytes_used) {
                    memcpy(c->text, cluster_utf8, output_bytes_used);
                    c->text_len = output_bytes_used;
                } else {
                    c->text_len = 0;
                    c->text_overflow = nullptr;
                }
            } else {
                cluster_utf8[output_bytes_used] = 0;
                termpaintp_set_overflow_text(surface, c, cluster_utf8);
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

void termpaint_surface_clear_with_attr_char(termpaint_surface *surface, const termpaint_attr *attr, int codepoint) {
    termpaint_surface_clear_rect_with_attr_char(surface, 0, 0, surface->width, surface->height, attr, codepoint);
}

void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg) {
    termpaint_surface_clear_rect(surface, 0, 0, surface->width, surface->height, fg, bg);
}

void termpaint_surface_clear_with_char(termpaint_surface *surface, int fg, int bg, int codepoint) {
    termpaint_surface_clear_rect_with_char(surface, 0, 0, surface->width, surface->height, fg, bg, codepoint);
}

static void termpaintp_surface_clear_rect_with_attr_and_string(termpaint_surface *surface, int x, int y,
                                                               int width, int height, const termpaint_attr *attr,
                                                               const unsigned char* str, unsigned len) {
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (width <= 0) return;
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
            if (str) {
                c->text_len = len;
                memcpy(c->text, str, len);
            } else {
                c->text_len = 0;
                c->text_overflow = nullptr;
            }
            c->bg_color = attr->bg_color;
            c->fg_color = attr->fg_color;
            c->deco_color = TERMPAINT_DEFAULT_COLOR;
            c->flags = attr->flags;
            c->attr_patch_idx = 0;
        }
    }
}

void termpaint_surface_clear_rect_with_attr(termpaint_surface *surface, int x, int y,
                                                               int width, int height, const termpaint_attr *attr) {
    termpaintp_surface_clear_rect_with_attr_and_string(surface, x, y, width, height, attr, nullptr, 0);
}

void termpaint_surface_clear_rect_with_attr_char(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr, int codepoint) {
    int codepointSanitized = replace_unusable_codepoints(codepoint);
    int codepointWidth = termpaintp_char_width(codepoint);
    if (codepoint == '\x7f' || codepointWidth != 1) {
        termpaint_surface_clear_rect_with_attr(surface, x, y, width, height, attr);
    } else {
        unsigned char buf[6];
        int len = termpaintp_encode_to_utf8(codepointSanitized, buf);
        termpaintp_surface_clear_rect_with_attr_and_string(surface, x, y, width, height, attr, buf, len);
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

void termpaint_surface_clear_rect_with_char(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg, int codepoint) {
    int codepointSanitized = replace_unusable_codepoints(codepoint);
    int codepointWidth = termpaintp_char_width(codepoint);
    if (codepoint == '\x7f' || codepointWidth != 1) {
        termpaint_surface_clear_rect(surface, x, y, width, height, fg, bg);
    } else {
        termpaint_attr attr;
        attr.fg_color = fg;
        attr.bg_color = bg;
        attr.deco_color = TERMPAINT_DEFAULT_COLOR;
        attr.flags = 0;
        attr.patch_setup = nullptr;
        attr.patch_cleanup = nullptr;
        termpaint_surface_clear_rect_with_attr_char(surface, x, y, width, height, &attr, codepointSanitized);
    }
}

void termpaint_surface_set_fg_color(const termpaint_surface *surface, int x, int y, unsigned fg) {
    if (x < 0) return;
    if (y < 0) return;
    if (x >= surface->width) return;
    if (y >= surface->height) return;
    cell* c = termpaintp_getcell(surface, x, y);

    if (c->text_len == 0 && c->text_overflow == WIDE_RIGHT_PADDING) {
        // only the first cell of a multi cell character can be used to change it.
        // This prevents duplicate changes with naive adjust each cell code.
        return;
    }

    c->fg_color = fg;
    for (int i = 0; i < c->cluster_expansion; i++) {
        cell* exp_cell = termpaintp_getcell(surface, x + 1 + i, y);
        exp_cell->fg_color = fg;
    }
}

void termpaint_surface_set_bg_color(const termpaint_surface *surface, int x, int y, unsigned bg) {
    if (x < 0) return;
    if (y < 0) return;
    if (x >= surface->width) return;
    if (y >= surface->height) return;
    cell* c = termpaintp_getcell(surface, x, y);

    if (c->text_len == 0 && c->text_overflow == WIDE_RIGHT_PADDING) {
        // only the first cell of a multi cell character can be used to change it.
        // This prevents duplicate changes with naive adjust each cell code.
        return;
    }

    c->bg_color = bg;
    for (int i = 0; i < c->cluster_expansion; i++) {
        cell* exp_cell = termpaintp_getcell(surface, x + 1 + i, y);
        exp_cell->bg_color = bg;
    }
}

void termpaint_surface_set_deco_color(const termpaint_surface *surface, int x, int y, unsigned deco_color) {
    if (x < 0) return;
    if (y < 0) return;
    if (x >= surface->width) return;
    if (y >= surface->height) return;
    cell* c = termpaintp_getcell(surface, x, y);

    if (c->text_len == 0 && c->text_overflow == WIDE_RIGHT_PADDING) {
        // only the first cell of a multi cell character can be used to change it.
        // This prevents duplicate changes with naive adjust each cell code.
        return;
    }

    c->deco_color = deco_color;
    for (int i = 0; i < c->cluster_expansion; i++) {
        cell* exp_cell = termpaintp_getcell(surface, x + 1 + i, y);
        exp_cell->deco_color = deco_color;
    }
}

void termpaint_surface_set_softwrap_marker(termpaint_surface *surface, int x, int y, bool state) {
    if (x < 0) return;
    if (y < 0) return;
    if (x >= surface->width) return;
    if (y >= surface->height) return;
    cell* c = termpaintp_getcell(surface, x, y);

    if (c->text_len == 0 && c->text_overflow == WIDE_RIGHT_PADDING) {
        // only the first cell of a multi cell character can be used to set the marker
        return;
    }

    if (state) {
        c->flags |= CELL_SOFTWRAP_MARKER;
    } else {
        c->flags &= ~CELL_SOFTWRAP_MARKER;
    }
}

bool termpaint_surface_resize_mustcheck(termpaint_surface *surface, int width, int height) {
    if (width < 0 || height < 0) {
        free(surface->cells);
        free(surface->cells_last_flush);
        termpaintp_collapse(surface);
    } else {
        if (!termpaintp_resize_mustcheck(surface, width, height)) {
            return false;
        }
    }
    return true;
}

void termpaint_surface_resize(termpaint_surface *surface, int width, int height) {
    if (!termpaint_surface_resize_mustcheck(surface, width, height)) {
        termpaintp_oom(surface->terminal);
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

static void termpaintp_surface_init(termpaint_surface *surface, termpaint_terminal *term) {
    surface->overflow_text.gc_mark_cb = termpaintp_surface_gc_mark_cb;
    surface->overflow_text.item_size = sizeof(termpaint_hash_item);
    surface->terminal = term;
}

termpaint_surface *termpaint_terminal_new_surface_or_nullptr(termpaint_terminal *term, int width, int height) {
    if (!term) {
        BUG("termpaint_terminal_new_surface with invalid terminal pointer");
    }
    termpaint_surface *ret = calloc(1, sizeof(termpaint_surface));
    if (!ret) {
        return nullptr;
    }
    termpaintp_surface_init(ret, term);
    termpaintp_collapse(ret);
    if (!termpaintp_resize_mustcheck(ret, width, height)) {
        termpaint_surface_free(ret);
        return nullptr;
    }
    return ret;
}

termpaint_surface *termpaint_terminal_new_surface(termpaint_terminal *term, int width, int height) {
    termpaint_surface *ret = termpaint_terminal_new_surface_or_nullptr(term, width, height);
    if (!ret) {
        termpaintp_oom(term);
    }
    return ret;
}

termpaint_surface *termpaint_surface_new_surface(termpaint_surface *surface, int width, int height) {
    return termpaint_terminal_new_surface(surface->terminal, width, height);
}

termpaint_surface *termpaint_surface_new_surface_or_nullptr(termpaint_surface *surface, int width, int height) {
    return termpaint_terminal_new_surface_or_nullptr(surface->terminal, width, height);
}

void termpaint_surface_free(termpaint_surface *surface) {
    if (!surface) {
        return;
    }

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

static void termpaintp_surface_copy_rect_same_surface(termpaint_surface *src_surface, int x, int y, int width, int height,
                                 int dst_x, int dst_y, int tile_left, int tile_right);


void termpaint_surface_copy_rect(termpaint_surface *src_surface, int x, int y, int width, int height,
                                 termpaint_surface *dst_surface, int dst_x, int dst_y, int tile_left, int tile_right) {
    if (x < 0) {
        width += x;
        dst_x -= x;
        x = 0;
        // also switch left mode to erase
        tile_left = TERMPAINT_COPY_NO_TILE;
    }
    if (y < 0) {
        dst_y -= y;
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
    if (dst_x + width > dst_surface->width) {
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

    if (src_surface == dst_surface) {
        termpaintp_surface_copy_rect_same_surface(src_surface, x, y, width, height, dst_x, dst_y, tile_left, tile_right);
        return;
    }

    for (int yOffset = 0; yOffset < height; yOffset++) {
        bool in_complete_cluster = false;
        int xOffset = 0;

        {
            cell *src_cell = termpaintp_getcell(src_surface, x, y + yOffset);
            if (src_cell->text_len == 0 && src_cell->text_overflow == WIDE_RIGHT_PADDING) {
                if (tile_left == TERMPAINT_COPY_TILE_PRESERVE) {
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
                            break;
                        }
                        if (i == width - 1) {
                            // whole line in src is one cluster, dst also has a cluster there
                            xOffset = width;
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
                            termpaintp_set_overflow_text(dst_surface, dst_scan, src_scan->text_overflow->text);
                        }
                    }
                }
            }

        }

        int extra_width = 0;

        for (; xOffset < width + extra_width; xOffset++) {
            cell *src_cell = termpaintp_getcell(src_surface, x + xOffset, y + yOffset);
            cell *dst_cell = termpaintp_getcell(dst_surface, dst_x + xOffset, dst_y + yOffset);

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
                        if (src_cell->text_overflow != nullptr) {
                            termpaintp_set_overflow_text(dst_surface, dst_cell, src_cell->text_overflow->text);
                        } else {
                            dst_cell->text_len = 0;
                            dst_cell->text_overflow = nullptr;
                        }
                    }
                } else {
                    dst_cell->text_len = 1;
                    dst_cell->text[0] = ' ';
                }
            }
        }
    }
}

static void termpaintp_surface_copy_rect_same_surface(termpaint_surface *dst_surface, int x, int y, int width, int height,
                                                      int dst_x, int dst_y, int tile_left, int tile_right) {
    // precondition: All rectangles are already fully within the surface.
    termpaint_surface *src_surface = termpaint_surface_new_surface(dst_surface,
                                                                   width + (x != 0 ? 1 : 0) + ((x + width != dst_surface->width) ? 1 : 0),
                                                                   height + (y != 0 ? 1 : 0) + ((y + height != dst_surface->height) ? 1 : 0));

    termpaint_surface_copy_rect(dst_surface, x - (x != 0 ? 1 : 0), y - (y != 0 ? 1 : 0), src_surface->width, src_surface->height,
                                src_surface, 0, 0,
                                TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    termpaint_surface_copy_rect(src_surface, (x != 0 ? 1 : 0), (y != 0 ? 1 : 0), width, height,
                                dst_surface, dst_x, dst_y,
                                tile_left, tile_right);

    termpaint_surface_free(src_surface);
}

termpaint_surface *termpaint_surface_duplicate(termpaint_surface *surface) {
    termpaint_surface *ret = termpaint_surface_new_surface(surface, surface->width, surface->height);

    termpaint_surface_copy_rect(surface, 0, 0, surface->width, surface->height,
                                ret, 0, 0,
                                TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);
    return ret;
}

unsigned termpaint_surface_peek_fg_color(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell_or_null(surface, x, y);
    if (!cell) {
        return 0;
    }
    return cell->fg_color;
}

unsigned termpaint_surface_peek_bg_color(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell_or_null(surface, x, y);
    if (!cell) {
        return 0;
    }
    return cell->bg_color;
}

unsigned termpaint_surface_peek_deco_color(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell_or_null(surface, x, y);
    if (!cell) {
        return 0;
    }
    return cell->deco_color;
}

int termpaint_surface_peek_style(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell_or_null(surface, x, y);
    if (!cell) {
        return 0;
    }
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
    cell *cell = termpaintp_getcell_or_null(surface, x, y);
    if (!cell || !cell->attr_patch_idx) {
        *setup = nullptr;
        *cleanup = nullptr;
        *optimize = true;
        return;
    }
    termpaintp_patch* patch = &surface->patches[cell->attr_patch_idx - 1];
    *setup = (const char *)patch->setup;
    *cleanup = (const char *)patch->cleanup;
    *optimize = patch->optimize;
}

const char *termpaint_surface_peek_text(const termpaint_surface *surface, int x, int y, int *len, int *left, int *right) {
    cell *cell = termpaintp_getcell_or_null(surface, x, y);
    if (!cell) {
        if (left) {
            *left = x;
        }
        if (right) {
            *right = x;
        }
        *len = 1;
        return TERMPAINT_ERASED;
    }
    while (x > 0) {
        if (cell->text_len != 0 || cell->text_overflow != WIDE_RIGHT_PADDING) {
            break;
        }
        --x;
        cell = termpaintp_getcell(surface, x, y);
    }

    if (left) {
        *left = x;
    }

    const char *text;
    if (cell->text_len > 0) {
        text = (const char*)cell->text;
        *len = cell->text_len;
    } else if (cell->text_overflow == nullptr) {
        text = TERMPAINT_ERASED;
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

bool termpaint_surface_peek_softwrap_marker(const termpaint_surface *surface, int x, int y) {
    cell *cell = termpaintp_getcell_or_null(surface, x, y);
    if (!cell) {
        return false;
    }
    return !!(cell->flags & CELL_SOFTWRAP_MARKER);
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
            if (termpaint_surface_peek_softwrap_marker(surface1, x, y)
                    != termpaint_surface_peek_softwrap_marker(surface2, x, y)) {
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

static void int_puts(termpaint_integration *integration, const char *str) {
    integration->p->write(integration, str, strlen(str));
}

static void int_uputs(termpaint_integration *integration, const unsigned char *str) {
    integration->p->write(integration, (const char*)str, ustrlen(str));
}

static void int_write(termpaint_integration *integration, const char *str, int len) {
    integration->p->write(integration, str, len);
}

static void int_debuglog(termpaint_terminal *term, const char *str, int len) {
    if (term->integration_vtbl->logging_func) {
        term->integration_vtbl->logging_func(term->integration, str, len);
    }
}

static void int_debuglog_puts(termpaint_terminal *term, const char *str) {
    int_debuglog(term, str, strlen(str));
}

static void int_debuglog_printf(termpaint_terminal *term, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buff[1024];
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);

    buff[sizeof(buff) - 1] = 0;
    int_debuglog_puts(term, buff);
}

static void int_write_printable(termpaint_integration *integration, const unsigned char *str, int len) {
    int input_bytes_used = 0;
    while (input_bytes_used < len) {
        int size = termpaintp_utf8_len(str[input_bytes_used]);

        // check termpaintp_utf8_decode_from_utf8 precondition
        if (input_bytes_used + size > len) {
            // bogus, bail
            return;
        }
        if (termpaintp_check_valid_sequence(str + input_bytes_used, size)) {
            int codepoint = termpaintp_utf8_decode_from_utf8(str + input_bytes_used, size);
            int new_codepoint = replace_unusable_codepoints(codepoint);
            if (codepoint == new_codepoint) {
                int_write(integration, (char*)str + input_bytes_used, size);
            } else {
                if (new_codepoint < 128) {
                    char ch;
                    ch = new_codepoint;
                    int_write(integration, &ch, 1);
                }
            }
        }
        input_bytes_used += size;
    }
}

static void int_put_num(termpaint_integration *integration, int num) {
    char buf[12];
    int len = sprintf(buf, "%d", num);
    integration->p->write(integration, buf, len);
}

static void int_put_tps(termpaint_integration *integration, const termpaint_str *tps) {
    integration->p->write(integration, (const char*)tps->data, (int)tps->len);
}

static void int_awaiting_response(termpaint_integration *integration) {
    if (integration->p->awaiting_response) {
        integration->p->awaiting_response(integration);
    }
}

static void int_restore_sequence_updated(termpaint_terminal *term) {
    termpaint_integration_private *vtbl = term->integration_vtbl;
    if (vtbl->restore_sequence_updated) {
        vtbl->restore_sequence_updated(term->integration, (char*)term->restore_seq.data, term->restore_seq.len);
    }
}

static void int_flush(termpaint_integration *integration) {
    integration->p->flush(integration);
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
    bool nonharmful = termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE);

    if (term->cursor_style != -1 && nonharmful) {
        const char *resetSequence = "\033[0 q";
        int cmd = term->cursor_style + (term->cursor_blink ? 0 : 1);
        if (term->cursor_style == TERMPAINT_CURSOR_STYLE_BAR
                && !termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE_BAR)) {
            // e.g. xterm < 282 does not support BAR style.
            cmd = TERMPAINT_CURSOR_STYLE_BLOCK + (term->cursor_blink ? 0 : 1);
        }
        if (cmd != term->cursor_prev_data) {
            termpaint_integration *integration = term->integration;
            if (termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_CURSOR_SHAPE_OSC50)) {
                // e.g. konsole. (konsole starting at version 18.07.70 could do the CSI space q one too, but
                // we don't have the konsole version.)
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
            termpaintp_prepend_str(&term->restore_seq, (const uchar*)resetSequence);
            int_restore_sequence_updated(term);
        }
        term->cursor_prev_data = cmd;
    }
}

static void termpaintp_input_event_callback(void *user_data, termpaint_event *event);
static bool termpaintp_input_raw_filter_callback(void *user_data, const char *data, unsigned length, _Bool overflow);

static void termpaint_color_entry_destroy(termpaint_color_entry *entry) {
    free(entry->saved);
    free(entry->requested);
}

static void termpaint_unpause_snippet_destroy(termpaint_unpause_snippet *entry) {
    termpaintp_str_destroy(&entry->sequences);
}

static void termpaintp_terminal_reset_capabilites(termpaint_terminal *terminal);

termpaint_terminal *termpaint_terminal_new_or_nullptr(termpaint_integration *integration) {
    termpaint_terminal *ret = calloc(1, sizeof(termpaint_terminal));
    if (!ret) {
        return nullptr;
    }
    termpaintp_surface_init(&ret->primary, ret);
    ret->primary.primary = true;
    // start collapsed
    termpaintp_collapse(&ret->primary);
    ret->integration = integration;
    ret->integration_vtbl = integration->p;

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
    termpaintp_terminal_reset_capabilites(ret);
    ret->terminal_type = TT_UNKNOWN;
    ret->terminal_type_confidence = 0;
    ret->max_csi_parameters = 15;
    ret->input = termpaint_input_new();
    if (!ret->input) {
        free(ret);
        return nullptr;
    }
    termpaint_input_set_event_cb(ret->input, termpaintp_input_event_callback, ret);
    termpaint_input_set_raw_filter_cb(ret->input, termpaintp_input_raw_filter_callback, ret);

    ret->colors.item_size = sizeof(termpaint_color_entry);
    ret->colors.destroy_cb = (void (*)(termpaint_hash_item*))termpaint_color_entry_destroy;

    ret->unpause_snippets.item_size = sizeof (termpaint_unpause_snippet);
    ret->unpause_snippets.destroy_cb = (void (*)(termpaint_hash_item*))termpaint_unpause_snippet_destroy;

    if (!termpaintp_str_preallocate(&ret->unpause_basic_setup, 64)) {
        termpaint_input_free(ret->input);
        free(ret);
        return nullptr;
    }

    if (!termpaintp_str_preallocate(&ret->restore_seq, 256)) {
        termpaintp_str_destroy(&ret->unpause_basic_setup);
        termpaint_input_free(ret->input);
        free(ret);
        return nullptr;
    }

    if (!termpaintp_str_preallocate(&ret->terminal_self_reported_name_version, 64)) {
        termpaintp_str_destroy(&ret->restore_seq);
        termpaintp_str_destroy(&ret->unpause_basic_setup);
        termpaint_input_free(ret->input);
        free(ret);
        return nullptr;
    }

    if (!termpaintp_str_preallocate(&ret->auto_detect_sec_device_attributes, 64)) {
        termpaintp_str_destroy(&ret->terminal_self_reported_name_version);
        termpaintp_str_destroy(&ret->restore_seq);
        termpaintp_str_destroy(&ret->unpause_basic_setup);
        termpaint_input_free(ret->input);
        free(ret);
        return nullptr;
    }

    termpaintp_prepend_str(&ret->restore_seq, (const uchar*)"\033[?25h\033[m");
    int_restore_sequence_updated(ret);

    return ret;
}

termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration) {
    termpaint_terminal *ret = termpaint_terminal_new_or_nullptr(integration);
    if (!ret) {
        termpaintp_oom_int(integration);
    }
    return ret;
}

void termpaint_terminal_glitch_on_out_of_memory(termpaint_terminal *term) {
    term->glitch_on_oom = true;
}

void termpaint_terminal_free(termpaint_terminal *term) {
    if (!term) {
        return;
    }

    termpaintp_str_destroy(&term->auto_detect_sec_device_attributes);
    termpaintp_str_destroy(&term->terminal_self_reported_name_version);
    termpaintp_surface_destroy(&term->primary);
    termpaintp_str_destroy(&term->restore_seq);
    termpaint_input_free(term->input);
    term->input = nullptr;
    term->integration_vtbl->free(term->integration);
    term->integration = nullptr;
    term->integration_vtbl = nullptr;
    termpaintp_str_destroy(&term->unpause_basic_setup);
    termpaintp_hash_destroy(&term->colors);
    termpaintp_hash_destroy(&term->unpause_snippets);
    free(term);
}

void termpaint_terminal_free_with_restore(termpaint_terminal *term) {
    if (!term) {
        return;
    }

    termpaint_integration *integration = term->integration;

    if (term->restore_seq.len) {
        int_write(integration, (const char*)term->restore_seq.data, term->restore_seq.len);
    }
    int_flush(integration);

    termpaint_terminal_free(term);
}

static void termpaintp_terminal_reset_capabilites(termpaint_terminal *terminal) {
    for (int i = 0; i < NUM_CAPABILITIES; i++) {
        terminal->capabilities[i] = false;
    }
    // cursor bar is on by default. Some terminals don't understand any sequence, for these this does not matter.
    // But some terminals recognize block and underline, but ignore bar. In this case it's better to remap bar to block.
    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE_BAR);

    // Start with assuming that terminals have in principle solid support for unicode fonts with common
    // linedrawing / semigraphics codepoints. But leave this to opt out for implementations with very
    // limited character repertoire.
    // One example is the kernel internal terminal in linux that is limited to 256 or 512 different glyphs total.
    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_EXTENDED_CHARSET);

    // Start with assuming a terminal might have true-color support. If a terminal (+ version) is known
    // not to support true-color this capability should be removed during detection. If a terminal (+ version)
    // is known to support true-color additionally set TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED during
    // detection
    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);

    // Most terminals support background color erase (bce) and allow multiple colors in cleared parts
    // of lines.
    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CLEARED_COLORING);

    // Most terminals support support 7-bit ST (ESC backslash) for terminating OSC/DCS sequences,
    // as that's what all traditional standards say.
    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_7BIT_ST);
}

inline bool termpaint_terminal_capable(const termpaint_terminal *terminal, int capability) {
    if (capability < 0 || capability >= NUM_CAPABILITIES) {
        return false;
    }
    return terminal->capabilities[capability];
}

static void termpaintp_update_cache_from_capabilities(termpaint_terminal *terminal) {
    terminal->cache_should_use_truecolor =
            termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED)
            || termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
}

void termpaint_terminal_promise_capability(termpaint_terminal *terminal, int capability) {
    if (capability < 0 || capability >= NUM_CAPABILITIES) {
        return;
    }
    terminal->capabilities[capability] = true;
    termpaintp_update_cache_from_capabilities(terminal);
}

void termpaint_terminal_disable_capability(termpaint_terminal *terminal, int capability) {
    if (capability < 0 || capability >= NUM_CAPABILITIES) {
        return;
    }
    terminal->capabilities[capability] = false;
    termpaintp_update_cache_from_capabilities(terminal);
}

bool termpaint_terminal_should_use_truecolor(termpaint_terminal *terminal) {
    return terminal->cache_should_use_truecolor;
}


termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term) {
    return &term->primary;
}

static int termpaintp_quantize_color_grid6(int val) {
    // index of nearest int to [0, 95, 135, 175, 215, 255]
    // cut points: 47, 115, 155, 195, 235
    if (val <= 47) {
        return 0;
    }
    if (val < 115) {
        return 1;
    }
    return 2 + (val - 115) / 40;
}

static int termpaintp_quantize_color_grid4(int val) {
    // index of nearest int to [0, 139, 205, 255]
    // cut points: 69, 172, 230
    if (val <= 172) {
        if (val <= 69) {
            return 0;
        } else {
            return 1;
        }
    } else {
        if (val < 230) {
            return 2;
        } else {
            return 3;
        }
    }
}

static const int termpaintp_grid6_values[] = {0, 95, 135, 175, 215, 255};
static const int termpaintp_grid4_values[] = {0, 139, 205, 255};

static const int termpaintp_ramp8_values[] = {46, 92, 115, 139, 162, 185, 208, 231};

static uint32_t termpaintp_quantize_color(termpaint_terminal *term, uint32_t color) {
    if (!term->cache_should_use_truecolor) {
        if ((color & 0xff000000) == TERMPAINT_RGB_COLOR_OFFSET) {
            const int r = (color >> 16) & 0xff;
            const int g = (color >> 8) & 0xff;
            const int b = (color) & 0xff;

            const int grey = (r+g+b) / 3;
            if (termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_88_COLOR)) {
                // find nearest color on grid ((r,g,b) for r,b,g in [0, 95, 135, 175, 215, 255])
                const int red_index = termpaintp_quantize_color_grid4(r);
                const int green_index = termpaintp_quantize_color_grid4(g);
                const int blue_index = termpaintp_quantize_color_grid4(b);

                const int red_quantized = termpaintp_grid4_values[red_index];
                const int green_quantized = termpaintp_grid4_values[green_index];
                const int blue_quantized = termpaintp_grid4_values[blue_index];

                color = TERMPAINT_INDEXED_COLOR + 16 + red_index * 16 + green_index * 4 + blue_index;
#define SQ(x) ((x) * (x))
                int best_metric = SQ(red_quantized - r) + SQ(green_quantized - g) + SQ(blue_quantized - b);

                for (int grey_index = 0; grey_index < 8; grey_index++) {
                    const int grey_quantized = termpaintp_ramp8_values[grey_index];
                    const int cur_metric = SQ(grey_quantized - r) + SQ(grey_quantized - g) + SQ(grey_quantized - b);
                    if (cur_metric < best_metric) {
                        color = TERMPAINT_INDEXED_COLOR + 80 + grey_index;
                        best_metric = cur_metric;
                    }
                }
#undef SQ
            } else {
                // find nearest grey value in [8, 18, ..., 228, 238]
                int grey_index = (((grey - 8)+5) / 10); // -3 / 10 rounds to zero, so grey == 0 still gives index 0
                if (grey_index >= 24) {
                    grey_index = 23;
                }
                const int grey_quantized = 8 + grey_index * 10;

                // find nearest color on grid ((r,g,b) for r,b,g in [0, 95, 135, 175, 215, 255])
                const int red_index = termpaintp_quantize_color_grid6(r);
                const int green_index = termpaintp_quantize_color_grid6(g);
                const int blue_index = termpaintp_quantize_color_grid6(b);

                const int red_quantized = termpaintp_grid6_values[red_index];
                const int green_quantized = termpaintp_grid6_values[green_index];
                const int blue_quantized = termpaintp_grid6_values[blue_index];

#define SQ(x) ((x) * (x))
                if ((SQ(grey_quantized - r) + SQ(grey_quantized - g) + SQ(grey_quantized - b))
                        < (SQ(red_quantized - r) + SQ(green_quantized - g) + SQ(blue_quantized - b))) {
#undef SQ
                    color = TERMPAINT_INDEXED_COLOR + 232 + grey_index;
                } else {
                    color = TERMPAINT_INDEXED_COLOR + 16
                            + red_index * 36
                            + green_index * 6
                            + blue_index;
                }
            }
        }
    }
    return color;
}

typedef struct {
    int index;
    int max;
} termpaintp_sgr_params;

static inline void write_color_sgr_values(termpaint_integration *integration, termpaintp_sgr_params *params, uint32_t color, char *direct, char *indexed, char *sep, unsigned named, unsigned bright_named) {
    if ((color & 0xff000000) == TERMPAINT_RGB_COLOR_OFFSET) {
        if (params->index + 5 >= params->max) {
            int_puts(integration, "m\033[");
            params->index = 0;
            int_puts(integration, direct + 1); // skip first ";"
        } else {
            int_puts(integration, direct);
        }
        int_put_num(integration, (color >> 16) & 0xff);
        int_puts(integration, sep);
        int_put_num(integration, (color >> 8) & 0xff);
        int_puts(integration, sep);
        int_put_num(integration, (color) & 0xff);
        params->index += 5;
    } else if (TERMPAINT_INDEXED_COLOR <= color && TERMPAINT_INDEXED_COLOR + 255 >= color) {
        if (params->index + 3 >= params->max) {
            int_puts(integration, "m\033[");
            params->index = 0;
            int_puts(integration, indexed + 1); // skip first ";"
        } else {
            int_puts(integration, indexed);
        }
        int_put_num(integration, (color) & 0xff);
        params->index += 3;
    } else {
        if (named) {
            if (TERMPAINT_NAMED_COLOR <= color && TERMPAINT_NAMED_COLOR + 7 >= color) {
                if (params->index + 1 >= params->max) {
                    int_puts(integration, "m\033[");
                    params->index = 0;
                } else {
                    int_puts(integration, ";");
                }
                int_put_num(integration, named + (color - TERMPAINT_NAMED_COLOR));
                params->index += 1;
            } else if (TERMPAINT_NAMED_COLOR + 8 <= color && TERMPAINT_NAMED_COLOR + 15 >= color) {
                if (params->index + 1 >= params->max) {
                    int_puts(integration, "m\033[");
                    params->index = 0;
                } else {
                    int_puts(integration, ";");
                }
                int_put_num(integration, bright_named + (color - (TERMPAINT_NAMED_COLOR + 8)));
                params->index += 1;
            }
        } else {
            if (TERMPAINT_NAMED_COLOR <= color && TERMPAINT_NAMED_COLOR + 15 >= color) {
                if (params->index + 3 >= params->max) {
                    int_puts(integration, "m\033[");
                    params->index = 0;
                    int_puts(integration, indexed + 1); // skip first ";"
                } else {
                    int_puts(integration, indexed);
                }
                int_put_num(integration, (color - TERMPAINT_NAMED_COLOR));
                params->index += 3;
            }
        }
    }
}

void termpaint_terminal_flush(termpaint_terminal *term, bool full_repaint) {
    termpaint_integration *integration = term->integration;
    full_repaint |= term->force_full_repaint;
    termpaintp_terminal_hide_cursor(term);
    int_puts(integration, "\e[H");
    char speculation_buffer[30];
    int speculation_buffer_state = 0; // 0 = cursor position matches current cell, -1 = force move, > 0 bytes to print instead of move
    int pending_row_move = 0;
    int pending_colum_move = 0;
    int pending_colum_move_digits = 1;
    int pending_colum_move_digits_step = 10;

    enum { sw_no, sw_single, sw_double } softwrap_prev = sw_no, softwrap = sw_no;

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
        bool cleared = false;

        softwrap = sw_no;
        if (y+1 < term->primary.height && term->primary.width) {
            cell* first_next_line = termpaintp_getcell(&term->primary, 0, y + 1);
            if (first_next_line->flags & CELL_SOFTWRAP_MARKER
                    && (first_next_line->text_len || first_next_line->text_overflow != nullptr)) {

                cell* last_this_line = termpaintp_getcell(&term->primary, term->primary.width - 1, y);
                if (last_this_line->flags & CELL_SOFTWRAP_MARKER
                        && (last_this_line->text_len || last_this_line->text_overflow != nullptr)) {
                    softwrap = sw_single;
                } else if (last_this_line->text_len == 0
                           && last_this_line->text_overflow == nullptr
                           && term->primary.width >= 2) {
                    last_this_line = termpaintp_getcell(&term->primary, term->primary.width - 2, y);
                    if (last_this_line->flags & CELL_SOFTWRAP_MARKER
                            && (last_this_line->text_len || last_this_line->text_overflow != nullptr)
                            && first_next_line->cluster_expansion == 1) {
                        softwrap = sw_double;
                    }
                }
            }
        }

        int first_noncopy_space = term->primary.width;
        if (termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_CLEARED_COLORING)) {
            if (softwrap == sw_no) {
                for (int x = term->primary.width - 1; x >= 0; x--) {
                    cell* c = termpaintp_getcell(&term->primary, x, y);
                    if ((c->text_len == 0 && c->text_overflow == nullptr)) {
                        first_noncopy_space = x;
                    } else {
                        break;
                    }
                }
            }
        }

        for (int x = 0; x < term->primary.width; x++) {
            cell* c = termpaintp_getcell(&term->primary, x, y);
            cell* old_c = &term->primary.cells_last_flush[y*term->primary.width+x];
            int code_units;
            bool text_changed;
            const unsigned char* text;
            if (c->text_len) {
                code_units = c->text_len;
                text = c->text;
                text_changed = old_c->text_len != c->text_len || memcmp(text, old_c->text, code_units) != 0;
            } else {
                if (c->text_overflow == nullptr) {
                    code_units = 1;
                    text = (const uchar*)" ";
                    text_changed = old_c->text_len || c->text_overflow != old_c->text_overflow;
                } else {
                    // TODO should we avoid crash here when cluster skipping failed?
                    code_units = strlen((char*)c->text_overflow->text);
                    text = c->text_overflow->text;
                    text_changed = old_c->text_len || c->text_overflow != old_c->text_overflow;
                }
            }

            uint32_t effective_fg_color = termpaintp_quantize_color(term, c->fg_color);
            uint32_t effective_bg_color = termpaintp_quantize_color(term, c->bg_color);

            bool needs_paint = full_repaint || effective_bg_color != old_c->bg_color || effective_fg_color != old_c->fg_color
                    || c->flags != old_c->flags || c->attr_patch_idx != old_c->attr_patch_idx || text_changed;

            uint32_t effective_deco_color;
            if (c->flags & CELL_ATTR_DECO_MASK) {
                effective_deco_color = c->deco_color;
                needs_paint |= effective_deco_color != old_c->deco_color;
            } else {
                effective_deco_color = TERMPAINT_DEFAULT_COLOR;
            }

            bool needs_attribute_change = effective_bg_color != current_bg || effective_fg_color != current_fg
                    || effective_deco_color != current_deco || (c->flags & CELL_ATTR_MASK) != current_flags
                    || c->attr_patch_idx != current_patch_idx;

            if (first_noncopy_space < x) {
                needs_paint = needs_attribute_change || (needs_paint && !cleared);
            }

            if (softwrap == sw_single && x == term->primary.width - 1) {
                needs_paint = true;
                if (term->did_terminal_disable_wrap) {
                    // terminals like urxvt, screen and libvterm need this before the cursor goes
                    // into pending wrap state.
                    int_puts(integration, "\033[?7h");
                }
            }

            if (softwrap == sw_double && x == term->primary.width - 2) {
                needs_paint = true;
                x += 1; // skip last cell
                if (term->did_terminal_disable_wrap) {
                    // terminals like urxvt, screen and libvterm need this before the cursor goes
                    // into pending wrap state.
                    int_puts(integration, "\033[?7h");
                }
            }

            if (softwrap_prev != sw_no) {
                needs_paint = true;
            }

            *old_c = *c;
            old_c->bg_color = effective_bg_color;
            old_c->fg_color = effective_fg_color;
            for (int i = 0; i < c->cluster_expansion; i++) {
                cell* wipe_c = &term->primary.cells_last_flush[y*term->primary.width+x+i+1];
                wipe_c->text_len = 1;
                wipe_c->text[0] = '\x01'; // impossible value, filtered out earlier in output pipeline
            }

            if (!needs_paint) {
                if (current_patch_idx) {
                    int_uputs(integration, term->primary.patches[current_patch_idx-1].cleanup);
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
                        } else if (speculation_buffer_state + code_units < (int)sizeof (speculation_buffer)) {
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
                        if (pending_colum_move != 1) {
                            int_put_num(integration, pending_colum_move);
                        }
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
                termpaintp_sgr_params params;
                params.index = 1;
                params.max = term->max_csi_parameters;
#define PUT_PARAMETER(s)                        \
    do { if (params.index + 1 >= params.max) {  \
        int_puts(integration, "m\033[");        \
        int_puts(integration, s + 1);           \
        params.index = 1;                       \
    } else {                                    \
        int_puts(integration, s);               \
        params.index += 1;                      \
    } } while (false)                           \
    /* end macro */
                write_color_sgr_values(integration, &params, effective_bg_color, ";48;2;", ";48;5;", ";", 40, 100);
                write_color_sgr_values(integration, &params, effective_fg_color, ";38;2;", ";38;5;", ";", 30, 90);
                write_color_sgr_values(integration, &params, effective_deco_color, ";58:2:", ";58:5:", ":", 0, 0);
                if (c->flags) {
                    if (c->flags & CELL_ATTR_BOLD) {
                        PUT_PARAMETER(";1");
                    }
                    if (c->flags & CELL_ATTR_ITALIC) {
                        PUT_PARAMETER(";3");
                    }
                    uint32_t underline = c->flags & CELL_ATTR_UNDERLINE_MASK;
                    if (underline == CELL_ATTR_UNDERLINE_SINGLE) {
                        PUT_PARAMETER(";4");
                    } else if (underline == CELL_ATTR_UNDERLINE_DOUBLE) {
                        PUT_PARAMETER(";21");
                    } else if (underline == CELL_ATTR_UNDERLINE_CURLY) {
                        // TODO maybe filter this by terminal capability somewhere?
                        if (params.index + 2 >= params.max) {
                            int_puts(integration, "m\033[");
                            int_puts(integration, "4:3");
                            params.index = 2;
                        } else {
                            int_puts(integration, ";4:3");
                            params.index += 2;
                        }
                    }
                    if (c->flags & CELL_ATTR_BLINK) {
                        PUT_PARAMETER(";5");
                    }
                    if (c->flags & CELL_ATTR_OVERLINE) {
                        PUT_PARAMETER(";53");
                    }
                    if (c->flags & CELL_ATTR_INVERSE) {
                        PUT_PARAMETER(";7");
                    }
                    if (c->flags & CELL_ATTR_STRIKE) {
                        PUT_PARAMETER(";9");
                    }
                }
                int_puts(integration, "m");
#undef PUT_PARAMETER
                current_bg = effective_bg_color;
                current_fg = effective_fg_color;
                current_deco = effective_deco_color;
                current_flags = c->flags & CELL_ATTR_MASK;

                if (current_patch_idx != c->attr_patch_idx) {
                    if (current_patch_idx) {
                        int_uputs(integration, term->primary.patches[current_patch_idx-1].cleanup);
                    }
                    if (c->attr_patch_idx) {
                        int_uputs(integration, term->primary.patches[c->attr_patch_idx-1].setup);
                    }
                }

                current_patch_idx = c->attr_patch_idx;
            }
            if (first_noncopy_space <= x) {
                int_write(integration, "\033[K", 3);
                pending_colum_move++;
                speculation_buffer_state = -1;
                cleared = true;
            } else {
                int_write(integration, (char*)text, code_units);
                if (softwrap_prev != sw_no) {
                    softwrap_prev = sw_no;
                    if (term->did_terminal_disable_wrap) {
                        int_puts(integration, "\033[?7l");
                    }
                }

                if (softwrap == sw_double && x == term->primary.width - 1) {
                    // clear gap cell when a double width character causes wrap
                    int_write(integration, "\033[K", 3);
                }
            }
            if (current_patch_idx) {
                if (!term->primary.patches[c->attr_patch_idx-1].optimize) {
                    int_uputs(integration, term->primary.patches[c->attr_patch_idx-1].cleanup);
                    current_patch_idx = 0;
                }
            }
            x += c->cluster_expansion;
        }

        if (current_patch_idx) {
            int_uputs(integration, term->primary.patches[current_patch_idx-1].cleanup);
            current_patch_idx = 0;
        }

        if (softwrap == sw_no) {
            if (full_repaint) {
                if (y+1 < term->primary.height) {
                    int_puts(integration, "\r\n");
                }
            } else {
                pending_row_move += 1;
            }
        }

        softwrap_prev = softwrap;
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

    if (term->cursor_x != -1 && term->cursor_y != -1) {
        termpaint_terminal_set_cursor(term, term->cursor_x, term->cursor_y);
    } else {
        if (pending_colum_move) {
            int_puts(integration, "\e[");
            if (pending_colum_move != 1) {
                int_put_num(integration, pending_colum_move);
            }
            int_puts(integration, "C");
        }
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
                int_uputs(integration, entry->base.text);
                int_puts(integration, ";");
                int_uputs(integration, entry->requested);
                if (termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_7BIT_ST)) {
                    int_puts(integration, "\033\\");
                } else {
                    int_puts(integration, "\a");
                }
            } else {
                int_puts(integration, "\033]1");
                int_uputs(integration, entry->base.text);
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
    termpaint_color_entry *entry = termpaintp_hash_ensure(&term->colors, (uchar*)buff);
    if (!entry) {
        termpaintp_oom(term);
    }
    sprintf(buff, "#%02x%02x%02x", r, g, b);
    if (entry->requested && ustrcmp(entry->requested, (uchar*)buff) == 0) {
        return;
    }

    if (color_slot == TERMPAINT_COLOR_SLOT_CURSOR) {
        // even requesting a color report does not allow to restore this, so just reset.
        // TODO: needs a sensible value for saved.
        entry->saved = (uchar*)strdup("");
        if (!entry->saved) {
            termpaintp_oom(term);
        }
        termpaintp_prepend_str(&term->restore_seq, (const uchar*)"\033]112\033\\");
        int_restore_sequence_updated(term);
    }

    if (!entry->save_initiated && !entry->saved) {
        termpaint_integration *integration = term->integration;
        int_puts(integration, "\033]");
        int_put_num(integration, color_slot);
        int_puts(integration, ";?\033\\");
        int_awaiting_response(integration);
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
    entry->requested = (uchar*)strdup(buff);
    if (!entry->requested) {
        termpaintp_oom(term);
    }
}

void termpaint_terminal_reset_color(termpaint_terminal *term, int color_slot) {
    char buff[100];
    sprintf(buff, "%d", color_slot);
    termpaint_color_entry *entry = termpaintp_hash_ensure(&term->colors, (uchar*)buff);
    if (!entry) {
        termpaintp_oom(term);
    }
    if (entry->saved) {
        if (!entry->dirty) {
            entry->dirty = true;
            entry->next_dirty = term->colors_dirty;
            term->colors_dirty = entry;
        }
        free(entry->requested);
        if (color_slot != TERMPAINT_COLOR_SLOT_CURSOR) {
            entry->requested = ustrdup(entry->saved);
            if (!entry->requested) {
                termpaintp_oom(term);
            }
        } else {
            entry->requested = nullptr;
        }
    }
}

static bool termpaintp_terminal_auto_detect_event(termpaint_terminal *terminal, termpaint_event *event);

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

static int termpaintp_parse_version(char *s) {
    int res = 0;
    int place = 0;
    int tmp = 0;
    for (; *s; s++) {
        if (termpaintp_char_ascii_num(*s)) {
            tmp = tmp * 10 + *s - '0';
        } else if (*s == '.') {
            if (place == 0) {
                res += tmp * 1000000;
            } else if (place == 1) {
                res += tmp * 1000;
            } else if (place == 2) {
                break;
            }
            tmp = 0;
            ++place;
        } else {
            break;
        }
    }
    if (place == 0) {
        res += tmp * 1000000;
    } else if (place == 1) {
        res += tmp * 1000;
    } else if (place == 2) {
        res += tmp;
        return res;
    }
    return res;
}

static void termpaintp_auto_detect_init_terminal_version_and_caps(termpaint_terminal *term) {
    if (termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_CSI_GREATER)) {
        // use TERMPAINT_CAPABILITY_CSI_GREATER as indication for more advanced parsing capabilities,
        // as there is no dedicated detection for this.
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_CSI_POSTFIX_MOD);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE);
    }

    if (term->terminal_type == TT_MISPARSING) {
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_EXTENDED_CHARSET);
    } else if (term->terminal_type == TT_TOODUMB) {
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_EXTENDED_CHARSET);
    } else if (term->terminal_type == TT_BASE) {
        if (!term->auto_detect_sec_device_attributes.len) {
            // This is primarily because of linux vc, see somment in TT_LINUX for details.
            // It's fairly easy for other terminals to work around by implementing ESC [ >c or ESC [ =c
            termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_EXTENDED_CHARSET);
        }
    } else if (term->terminal_type == TT_VTE) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
        if (term->auto_detect_sec_device_attributes.len > 11) {
            const unsigned char* data = term->auto_detect_sec_device_attributes.data;
            bool vte_gt0_54 = memcmp(data, "\033[>65;", 6) == 0;
            bool vte_old = memcmp(data, "\033[>1;", 5) == 0;
            if (vte_gt0_54 || vte_old) {
                if (vte_old) {
                    data += 5;
                } else {
                    data += 6;
                }
                int version = 0;
                while (termpaintp_char_ascii_num(*data)) {
                    version = version * 10 + *data - '0';
                    ++data;
                }
                if (*data == ';' && (version < 5400) == vte_old) {
                    term->terminal_version = version;

                    if (term->terminal_version < 4000) {
                        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE);
                    } else {
                        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE);
                    }

                    if (term->terminal_version >= 5400) {
                        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TITLE_RESTORE);
                    }
                    if (term->terminal_version < 5400) {
                        // fragile dictinary base parsing.
                        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_CSI_GREATER);
                        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_CSI_EQUALS);
                        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_CSI_POSTFIX_MOD);
                    }

                }
            }
        }
        if (term->terminal_version < 3600) {
            termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);
        } else {
            termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
        }
    } else if (term->terminal_type == TT_XTERM) {
        if (term->auto_detect_sec_device_attributes.len > 10) {
            const unsigned char* data = term->auto_detect_sec_device_attributes.data;
            while (*data != ';' && *data != 0) {
                ++data;
            }
            if (*data == ';') {
                ++data;
                int version = 0;
                while (termpaintp_char_ascii_num(*data)) {
                    version = version * 10 + *data - '0';
                    ++data;
                }
                if (*data == ';') {
                    term->terminal_version = version;
                    if (term->terminal_version < 282) {
                        // xterm < 282 does not support BAR style. Enable remapping.
                        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE_BAR);
                    }
                }
            }
        }
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TITLE_RESTORE);
        if (term->terminal_version < 282) {
            termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);
        } else {
            termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
        }
        // tab is converted to space by the default settings starting from version 333.
        // But that's true regardless of state of bracketed paste.
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
    } else if (term->terminal_type == TT_SCREEN) {
        const unsigned char* data = term->auto_detect_sec_device_attributes.data;
        if (term->auto_detect_sec_device_attributes.len > 10 && memcmp(data, "\033[>83;", 6) == 0) {
            data += 6;
            int version = 0;
            while (termpaintp_char_ascii_num(*data)) {
                version = version * 10 + *data - '0';
                ++data;
            }
            if (*data == ';') {
                term->terminal_version = version;
            }
        }
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_CLEARED_COLORING);
    } else if (term->terminal_type == TT_TMUX) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
    } else if (term->terminal_type == TT_KONSOLE) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
        // konsole starting at version 18.07.70 could do the CSI space q one too, but
        // we don't have the konsole version.
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_CURSOR_SHAPE_OSC50);
        // konsole starting at version 19.08.2 supports 7-bit ST, but
        // we don't have the konsole version.
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_7BIT_ST);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
    } else if (term->terminal_type == TT_URXVT) {
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);
        // XXX: urxvt 9.19 seems to crash on bracketed paste, so don't set it
        // at least till 9.22 urxvt sends just ESC as terminator in replies when using ESC \ in the request.
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_7BIT_ST);
    } else if (term->terminal_type == TT_LINUXVC) {
        // Linux VC has to fit all character choices into 8 bit or 9 bit (depending on config)
        // thus is has a very limited set of characters available. What is available exactly
        // depends on the font, with most fonts optimizing for a given set of languages and
        // only providing a small set of line drawing characters etc.
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_EXTENDED_CHARSET);
    } else if (term->terminal_type == TT_MACOS) {
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);
        // does background color erase (bce) but does not allow multiple colors of cleared cells
        termpaint_terminal_disable_capability(term, TERMPAINT_CAPABILITY_CLEARED_COLORING);
    } else if (term->terminal_type == TT_TERMINOLOGY) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
        // To get here terminology has to be at least 1.4 (first version to support DA3)

        if (term->terminal_self_reported_name_version.len) {
            char *version_part = strchr((const char*)term->terminal_self_reported_name_version.data, ' ');
            if (version_part) {
                term->terminal_version = termpaintp_parse_version(version_part + 1);
            }
        }

        // terminology approximates to 256 color palette internally (since 1.2.0), but that's ok.
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);

        if (term->terminal_version >= 1007000) { // supported since 1.7.0
            termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TITLE_RESTORE);
        }

        // all shapes have been added in 1.2 so this is always safe.
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE_BAR);
    } else if (term->terminal_type == TT_MINTTY) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
        const unsigned char* data = term->auto_detect_sec_device_attributes.data;
        if (term->auto_detect_sec_device_attributes.len > 10 && memcmp(data, "\033[>77;", 6) == 0) {
            data += 6;
            int version = 0;
            while (termpaintp_char_ascii_num(*data)) {
                version = version * 10 + *data - '0';
                ++data;
            }
            if (*data == ';') {
                term->terminal_version = version;
            }
        }
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TITLE_RESTORE);
    } else if (term->terminal_type == TT_KITTY) {
        if (term->auto_detect_sec_device_attributes.len > 5
                && termpaintp_string_prefix((const uchar*)"\033[>1;", term->auto_detect_sec_device_attributes.data, term->auto_detect_sec_device_attributes.len)) {
            int val = 0;
            for (const unsigned char *tmp = term->auto_detect_sec_device_attributes.data + 5; *tmp; tmp++) {
                if (termpaintp_char_ascii_num(*tmp)) {
                    val = val * 10 + (*tmp - '0');
                } else if (*tmp == ';') {
                    if (val >= 4000) {
                        int version = (val - 4000 ) * 1000;
                        val = 0;
                        tmp++;
                        for (; *tmp; tmp++) {
                            if (termpaintp_char_ascii_num(*tmp)) {
                                val = val * 10 + (*tmp - '0');
                            } else if (*tmp == ';' || *tmp == 'c') {
                                if (val < 1000) {
                                    version += val;
                                } else {
                                    version += 999;
                                }
                                term->terminal_version = version;
                            } else {
                                break;
                            }
                        }
                    }
                    break;
                } else {
                    break;
                }
            }
        }
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TITLE_RESTORE);
    } else if (term->terminal_type == TT_ITERM2) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
    } else if (term->terminal_type == TT_MLTERM) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
        term->max_csi_parameters = 10;
    } else if (term->terminal_type == TT_MSFT_TERMINAL) {
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
    } else if (term->terminal_type == TT_FULL) {
        // full is promised to claim support for everything
        // But TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT, TERMPAINT_CAPABILITY_CSI_GREATER
        // and TERMPAINT_CAPABILITY_CSI_EQUALS are detected in main finger printing.
        // 88_COLOR disables 256 color support and is quite rxvt-unicode specific
        // CURSOR_SHAPE_OSC50 is konsole (and derived) specific, general terminals are expected to use
        // the ESC[ VAL SP q  sequence for cursor shape and blink setup.
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TITLE_RESTORE);
        termpaint_terminal_promise_capability(term, TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED);
    }
}

void termpaint_terminal_auto_detect_apply_input_quirks(termpaint_terminal *terminal, bool backspace_is_x08) {
    // For now apply backspace_is_x08 for all terminal types. The can be tuned to disregard this for
    // specific types when needed.

    // Note: terminology does not support ctrl-backspace in backspace_is_x08 mode, but does when !backspace_is_x08.
    //       so it seems swapped is still a ok mapping.
    if (backspace_is_x08) {
        termpaint_input_activate_quirk(terminal->input, TERMPAINT_INPUT_QUIRK_BACKSPACE_X08_AND_X7F_SWAPPED);
    }
}

static void termpaintp_input_event_callback(void *user_data, termpaint_event *event) {
    termpaint_terminal *term = user_data;
    if (term->ad_state == AD_NONE || term->ad_state == AD_FINISHED) {
        if (event->type == TERMPAINT_EV_COLOR_SLOT_REPORT) {
            char buff[100];
            sprintf(buff, "%d", event->color_slot_report.slot);
            termpaint_color_entry *entry = termpaintp_hash_ensure(&term->colors, (uchar*)buff);
            if (entry) { // entry may be nullptr if not requested via termpaint and out of memory
                if (!entry->saved) {
                    entry->saved = (uchar*)strndup(event->color_slot_report.color,
                                                   event->color_slot_report.length);
                    if (!entry->saved) {
                        termpaintp_oom(term);
                    }
                    termpaintp_prepend_str(&term->restore_seq, (const uchar*)"\033\\");
                    termpaintp_prepend_str(&term->restore_seq, entry->saved);
                    termpaintp_prepend_str(&term->restore_seq, (const uchar*)";");
                    termpaintp_prepend_str(&term->restore_seq, entry->base.text);
                    termpaintp_prepend_str(&term->restore_seq, (const uchar*)"\033]");
                    int_restore_sequence_updated(term);
                    if (entry->requested && !entry->dirty) {
                        entry->dirty = true;
                        entry->next_dirty = term->colors_dirty;
                        term->colors_dirty = entry;
                        term->request_repaint = true;
                    }
                }
            }
        }
        term->event_cb(term->event_user_data, event);
    } else {
        termpaintp_terminal_auto_detect_event(term, event);
        int_flush(term->integration);
        if (term->ad_state == AD_FINISHED) {
            termpaintp_auto_detect_init_terminal_version_and_caps(term);

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
        int_awaiting_response(integration);
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
        if (term->integration_vtbl->request_callback) {
            term->integration_vtbl->request_callback(term->integration);
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

void termpaint_terminal_handle_paste(termpaint_terminal *term, bool enabled) {
    termpaint_input_handle_paste(term->input, enabled);
}

void termpaint_terminal_expect_apc_input_sequences(termpaint_terminal *term, bool enabled) {
    termpaint_input_expect_apc_sequences(term->input, enabled);
}

void termpaint_terminal_activate_input_quirk(termpaint_terminal *term, int quirk) {
    termpaint_input_activate_quirk(term->input, quirk);
}

static void termpaintp_patch_misparsing_defered(termpaint_terminal *terminal, termpaint_integration *integration,
                                        auto_detect_state next_state) {
    terminal->ad_state = AD_GLITCH_PATCHING;
    terminal->glitch_patching_next_state = next_state;

    int reset_x = terminal->initial_cursor_x;
    int reset_y = terminal->initial_cursor_y;

    if ((terminal->initial_cursor_y == terminal->glitch_cursor_y) && (terminal->initial_cursor_x > terminal->glitch_cursor_x)) {
        // when starting on the last line a line wrap caused by glitches will scroll and thus the cursor seemingly
        // just moves left on the same line. Gliches start on the line that has scrolled up when wraping thus start on
        // that line
        reset_y -= 1;
    }

    int_puts(integration, "\033[");
    int_put_num(integration, reset_y + 1);
    int_puts(integration, ";");
    int_put_num(integration, reset_x + 1);
    int_puts(integration, "H");
    int_puts(integration, " ");
    if (termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT)) {
        int_puts(integration, "\033[?6n");
    } else {
        int_puts(integration, "\033[6n");
        termpaint_terminal_expect_cursor_position_report(terminal);
    }
}

static void termpaintp_patch_misparsing_from_event(termpaint_terminal *terminal, termpaint_integration *integration,
                                        termpaint_event *event, auto_detect_state next_state) {
    terminal->glitch_cursor_x = event->cursor_position.x;
    terminal->glitch_cursor_y = event->cursor_position.y;
    termpaintp_patch_misparsing_defered(terminal, integration, next_state);
}

static void termpaintp_terminal_auto_detect_prepare_self_reporting(termpaint_terminal *terminal, int new_state) {
    termpaint_integration *integration = terminal->integration;

    int_puts(integration, "\033[>q");
    bool might_be_kitty = false;
    bool might_be_iterm2 = false;
    bool might_be_mlterm = false;
    if (terminal->auto_detect_sec_device_attributes.len) {
        const int attr_len = terminal->auto_detect_sec_device_attributes.len;
        if (termpaintp_string_prefix((const uchar*)"\033[>1;", terminal->auto_detect_sec_device_attributes.data, attr_len)) {
            int val = 0;
            for (const unsigned char *tmp = terminal->auto_detect_sec_device_attributes.data + 5; *tmp; tmp++) {
                if (termpaintp_char_ascii_num(*tmp)) {
                    val = val * 10 + (*tmp - '0');
                } else if (*tmp == ';') {
                    if (val >= 4000) {
                        might_be_kitty = true;
                    }
                    break;
                } else {
                    break;
                }
            }
        }

        might_be_iterm2 = (!terminal->seen_dec_terminal_param
                           && attr_len == 10
                           && memcmp(terminal->auto_detect_sec_device_attributes.data, "\033[>0;95;0c", 10) == 0);
        might_be_mlterm = (terminal->seen_dec_terminal_param
                           && attr_len == 12
                           && memcmp(terminal->auto_detect_sec_device_attributes.data, "\033[>24;279;0c", 10) == 0);
    }
    if (might_be_kitty || might_be_iterm2 || might_be_mlterm) {
        int_puts(integration, "\033P+q544e\033\\");
    }
    int_puts(integration, "\033[5n");
    int_awaiting_response(integration);
    terminal->ad_state = new_state;
}

// known terminals where auto detections hangs: freebsd system console using vt module
static bool termpaintp_terminal_auto_detect_event(termpaint_terminal *terminal, termpaint_event *event) {
    termpaint_integration *integration = terminal->integration;

    if (event == nullptr) {
        terminal->ad_state = AD_INITIAL;
    }

    switch (terminal->ad_state) {
        case AD_NONE:
        case AD_FINISHED:
            // should not happen
            break;
        case AD_INITIAL:
            terminal->glitch_cursor_y = -1; // disarmed glitch patching state
            termpaint_input_expect_cursor_position_report(terminal->input);
            termpaint_input_expect_cursor_position_report(terminal->input);
            int_puts(integration, "\033[5n");
            int_puts(integration, "\033[6n");
            int_puts(integration, "\033[>c");
            int_puts(integration, "\033[6n");
            int_puts(integration, "\033[5n");
            int_awaiting_response(integration);
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
                terminal->ad_state = AD_BASIC_REQ_FAILED_CURPOS_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_CHAR && event->c.length == 1
                       && event->c.string[0] == '0' && event->c.modifier == TERMPAINT_MOD_ALT) {
                // hterm has a long standing typo in it's \033[5n reply that replys with a missing '['
                terminal->terminal_type = TT_INCOMPATIBLE;
                terminal->ad_state = AD_HTERM_RECOVERY1;
                return true;
            }
            break;
        case AD_BASIC_REQ_FAILED_CURPOS_RECVED:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                // no use, but still need to wait for cursor report reply
                terminal->ad_state = AD_BASIC_REQ_FAILED_CURPOS_RECVED;
                return true;
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
                terminal->ad_state = AD_EXPECT_SYNC_TO_FINISH;
                return true;
            } else if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_FINISHED;
                return false;
            }
            break;
        case AD_BASIC_CURPOS_RECVED:
            if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_GREATER);
                termpaintp_str_assign_n(&terminal->auto_detect_sec_device_attributes, event->raw.string, event->raw.length);
                if (event->raw.length > 6 && memcmp("\033[>85;", event->raw.string, 6) == 0) {
                    // urxvt source says: first parameter is 'U' / 85 for urxvt (except for 7.[34])
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                    terminal->terminal_type = TT_URXVT;
                    terminal->terminal_type_confidence = 2;
                }
                if (event->raw.length > 6 && memcmp("\033[>83;", event->raw.string, 6) == 0) {
                    // 83 = 'S'
                    // second parameter is version as major*10000 + minor * 100 + patch
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                    terminal->terminal_type = TT_SCREEN;
                    terminal->terminal_type_confidence = 2;
                }
                if (event->raw.length > 6 && memcmp("\033[>84;", event->raw.string, 6) == 0) {
                    // 84 = 'T'
                    // no version here
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                    terminal->terminal_type = TT_TMUX;
                    terminal->terminal_type_confidence = 2;
                }
                if (event->raw.length > 6 && memcmp("\033[>77;", event->raw.string, 6) == 0) {
                    // 77 = 'M'
                    // second parameter is version as major*10000 + minor * 100 + patch
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                    terminal->terminal_type = TT_MINTTY;
                    terminal->terminal_type_confidence = 2;
                }

                terminal->ad_state = AD_BASIC_SEC_DEV_ATTRIB_RECVED_CONSUME_CURPOS;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_PRI_DEV_ATTRIB) {
                // We never asked for primary device attributes. This means the terminal gets
                // basic parsing rules wrong.
                terminal->terminal_type = TT_TOODUMB;
                terminal->ad_state = AD_WAIT_FOR_SYNC_TO_FINISH;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                // check if finger printing left printed characters
                if (terminal->initial_cursor_x == event->cursor_position.x
                        && terminal->initial_cursor_y == event->cursor_position.y) {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_GREATER);
                    terminal->ad_state = AD_BASIC_CURPOS_RECVED_NO_SEC_DEV_ATTRIB;

                    return true;
                } else {
                    termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_CSI_GREATER);
                    terminal->terminal_type = TT_MISPARSING;
                    // prepare defered glitch patching
                    terminal->glitch_cursor_x = event->cursor_position.x;
                    terminal->glitch_cursor_y = event->cursor_position.y;
                    terminal->ad_state = AD_BASIC_NO_SEC_DEV_ATTRIB_MISPARSING;
                    return true;
                }
            }
            break;
        case AD_BASIC_NO_SEC_DEV_ATTRIB_MISPARSING:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                termpaintp_patch_misparsing_defered(terminal, integration, AD_FINISHED);
                return true;
            }
            break;
        case AD_BASIC_CURPOS_RECVED_NO_SEC_DEV_ATTRIB:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_GREATER);

                int_puts(integration, "\033[=c");
                int_puts(integration, "\033[>1c");
                int_puts(integration, "\033[?6n");
                int_puts(integration, "\033[1x");
                int_puts(integration, "\033[5n");
                int_awaiting_response(integration);
                terminal->ad_state = AD_FP1_REQ;
                return true;
            }
            break;
        case AD_BASIC_SEC_DEV_ATTRIB_RECVED_CONSUME_CURPOS:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                terminal->ad_state = AD_BASIC_SEC_DEV_ATTRIB_RECVED;
                return true;
            }
            break;
        case AD_BASIC_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                if (terminal->terminal_type_confidence >= 2) {
                    if (terminal->terminal_type == TT_URXVT) {
                        // auto detect 88 or 256 color mode by observing if querying color 255 results in a response
                        termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_88_COLOR);
                        // Using BEL as termination, because urxvt doesn't properly support ESC \ as terminator
                        // at least till 9.22 urxvt sends just ESC as terminator when using ESC \ in the request.
                        int_puts(integration, "\033]4;255;?\007");
                        int_puts(integration, "\033[5n");
                        terminal->ad_state = AD_URXVT_88_256_REQ;
                        return true;
                    } else {
                        termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                        return true;
                    }
                }
                int_puts(integration, "\033[=c");
                int_puts(integration, "\033[>1c");
                int_puts(integration, "\033[?6n");
                int_puts(integration, "\033[1x");
                int_puts(integration, "\033[5n");
                int_awaiting_response(integration);
                terminal->ad_state = AD_FP1_REQ;
                return true;
            }
            break;
        case AD_URXVT_88_256_REQ:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                return true;
            } else if (event->type == TERMPAINT_EV_PALETTE_COLOR_REPORT) {
                termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_88_COLOR);
                return true;
            }
            break;
        case AD_FP1_REQ:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                if (terminal->terminal_type_confidence == 0) {
                    terminal->terminal_type = TT_BASE;
                }
                termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                // see if "\033[=c" was misparsed
                termpaint_input_expect_cursor_position_report(terminal->input);
                int_puts(integration, "\033[6n");
                int_awaiting_response(integration);
                terminal->ad_state = AD_FP1_CLEANUP;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_3RD_DEV_ATTRIB) {
                termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                if (event->raw.length == 8) {
                    // Terminal implementors: DO NOT fake other terminal IDs here!
                    // any unknown id will enable all features here. Allocate a new one!
                    if (memcmp(event->raw.string, "7E565445", 8) == 0) { // ~VTE
                        terminal->terminal_type = TT_VTE;
                        terminal->terminal_type_confidence = 2;
                    } else if (memcmp(event->raw.string, "7E7E5459", 8) == 0) { // ~~TY
                        terminal->terminal_type = TT_TERMINOLOGY;
                        terminal->terminal_type_confidence = 2;
                    } else if (memcmp(event->raw.string, "7E4C4E58", 8) == 0) { // ~LNX
                        terminal->terminal_type = TT_LINUXVC;
                        terminal->terminal_type_confidence = 2;
                    } else if (memcmp(event->raw.string, "00000000", 8) == 0) {
                        // xterm uses this since 336. But this could be something else too.
                        // Microsoft Terminal uses this as well.
                        terminal->terminal_type = TT_BASE;
                        if (terminal->auto_detect_sec_device_attributes.len
                                && ustr_eq(terminal->auto_detect_sec_device_attributes.data, (const uchar*)"\033[>0;10;1c")) {
                            terminal->terminal_type = TT_MSFT_TERMINAL;
                            terminal->terminal_type_confidence = 1;
                        } else {
                            if (terminal->auto_detect_sec_device_attributes.len > 10) {
                                const unsigned char* data = terminal->auto_detect_sec_device_attributes.data;
                                while (*data != ';' && *data != 0) {
                                    ++data;
                                }
                                if (*data == ';') {
                                    ++data;
                                    int version = 0;
                                    while (termpaintp_char_ascii_num(*data)) {
                                        version = version * 10 + *data - '0';
                                        ++data;
                                    }
                                    if (*data == ';') {
                                        if (version >= 336) {
                                            terminal->terminal_type = TT_XTERM;
                                            terminal->terminal_type_confidence = 1;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        terminal->terminal_type = TT_FULL;
                        terminal->terminal_type_confidence = 1;
                    }
                    terminal->ad_state = AD_FP1_REQ_TERMID_RECVED;
                } else if (event->raw.length == 1 && event->raw.string[0] == '0') {
                    // xterm uses this between 280 and 335. But this could be something else too.
                    if (terminal->auto_detect_sec_device_attributes.len == 12
                            && memcmp(terminal->auto_detect_sec_device_attributes.data, "\033[>41;", 6) == 0
                            && termpaintp_char_ascii_num(terminal->auto_detect_sec_device_attributes.data[6])
                            && termpaintp_char_ascii_num(terminal->auto_detect_sec_device_attributes.data[7])
                            && termpaintp_char_ascii_num(terminal->auto_detect_sec_device_attributes.data[8])
                            && memcmp(terminal->auto_detect_sec_device_attributes.data + 9, ";0c", 3) == 0) {
                        int version = (terminal->auto_detect_sec_device_attributes.data[6] - '0') * 100
                                + (terminal->auto_detect_sec_device_attributes.data[7] - '0') * 10
                                + terminal->auto_detect_sec_device_attributes.data[8] - '0';
                        if (280 <= version && version <= 335) {
                            terminal->terminal_type = TT_XTERM;
                            terminal->terminal_type_confidence = 1;
                            terminal->ad_state = AD_FP1_REQ_TERMID_RECVED;
                        }
                    }
                }
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP1_SEC_DEV_ATTRIB_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if (event->cursor_position.safe) {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                } else {
                    termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                }
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                    // prepare defered glitch patching
                    terminal->glitch_cursor_x = event->cursor_position.x;
                    terminal->glitch_cursor_y = event->cursor_position.y;
                    terminal->terminal_type = TT_BASE;
                } else {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                    terminal->terminal_type = TT_BASE;
                }
                terminal->ad_state = AD_FP1_QMCURSOR_POS_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->seen_dec_terminal_param = true;
                if (terminal->terminal_type_confidence == 0) {
                    terminal->terminal_type = TT_BASE;
                }
                termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                terminal->ad_state = AD_FP1_CLEANUP_AFTER_SYNC;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_PRI_DEV_ATTRIB) {
                // For terminals that misinterpret \033[=c as \033[c
                terminal->ad_state = AD_FP1_3RD_DEV_ATTRIB_ALIASED_TO_PRI;
                return true;
            }
            break;
        case AD_FP1_3RD_DEV_ATTRIB_ALIASED_TO_PRI:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->terminal_type = TT_BASE;
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->seen_dec_terminal_param = true;
                terminal->terminal_type = TT_MACOS;
                terminal->ad_state = AD_EXPECT_SYNC_TO_FINISH;
                return true;
            } else {
                terminal->terminal_type = TT_BASE;
                terminal->ad_state = AD_WAIT_FOR_SYNC_TO_FINISH;
                return true;
            }
            break;
        case AD_FP1_CLEANUP:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                    termpaintp_patch_misparsing_from_event(terminal, integration, event, AD_FINISHED);
                    return true;
                } else {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                    termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                    return true;
                }
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
                if (termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT)) {
                    int_puts(integration, "\033[?6n");
                } else {
                    termpaint_input_expect_cursor_position_report(terminal->input);
                    int_puts(integration, "\033[6n");
                }
                int_awaiting_response(integration);
                terminal->ad_state = AD_FP1_CLEANUP;
                return true;
            }
        break;
        case AD_WAIT_FOR_SYNC_TO_SELF_REPORTING:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                return true;
            } else {
                if (event->type != TERMPAINT_EV_KEY && event->type != TERMPAINT_EV_CHAR) {
                    return true;
                }
            }
            break;
        case AD_EXPECT_SYNC_TO_SELF_REPORTING:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                return true;
            }
            break;
        case AD_SELF_REPORTING:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                terminal->ad_state = AD_FINISHED;
                return false;
            } else if (event->type == TERMPAINT_EV_RAW_TERM_NAME) {
                terminal->ad_state = AD_SELF_REPORTING;
                termpaintp_str_assign_n(&terminal->terminal_self_reported_name_version, event->raw.string, event->raw.length);
                if (termpaintp_string_prefix((const uchar*)"terminology ", (const uchar*)event->raw.string, event->raw.length)) {
                    terminal->terminal_type = TT_TERMINOLOGY;
                }
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_TERMINFO_QUERY_REPLY) {
                terminal->ad_state = AD_SELF_REPORTING;
                if (event->raw.length >= 8 && event->raw.string[0] == '1') { // only successful/valid reports
                    if (termpaintp_mem_ascii_case_insensitive_equals(event->raw.string + 3, "544e=", 5)) {
                        if (event->raw.length == 30
                                && termpaintp_mem_ascii_case_insensitive_equals(event->raw.string + 8, "787465726d2d6b69747479", 22)) {
                            terminal->terminal_type = TT_KITTY;
                        }
                        if (event->raw.length == 20
                                && termpaintp_mem_ascii_case_insensitive_equals(event->raw.string + 8, "695465726d32", 12)) {
                            terminal->terminal_type = TT_ITERM2;
                        }
                        if (event->raw.length == 20
                                && termpaintp_mem_ascii_case_insensitive_equals(event->raw.string + 8, "6D6C7465726D", 12)) {
                            terminal->terminal_type = TT_MLTERM;
                        }
                    }
                }
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
                termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                terminal->ad_state = AD_FP1_REQ_TERMID_RECVED_SEC_DEV_ATTRIB_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                // keep terminal_type from terminal id
                if (event->cursor_position.safe) {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                } else {
                    termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                }
                terminal->ad_state = AD_WAIT_FOR_SYNC_TO_SELF_REPORTING;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->seen_dec_terminal_param = true;
                termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                terminal->ad_state = AD_EXPECT_SYNC_TO_SELF_REPORTING;
                return true;
            }
            break;
        case AD_FP1_REQ_TERMID_RECVED_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if (event->cursor_position.safe) {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                } else {
                    termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                }
                terminal->ad_state = AD_WAIT_FOR_SYNC_TO_SELF_REPORTING;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->seen_dec_terminal_param = true;
                // ignore
                return true;
            }
            break;
        case AD_FP1_SEC_DEV_ATTRIB_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                termpaint_input_expect_cursor_position_report(terminal->input);
                int_puts(integration, "\033[6n"); // detect if "\033[=c" was misparsed
                int_puts(integration, "\033[>0;1c");
                int_puts(integration, "\033[5n");
                int_awaiting_response(integration);
                terminal->ad_state = AD_FP2_REQ;
                return true;
            } else if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if (event->cursor_position.safe) {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                } else {
                    termpaint_terminal_disable_capability(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT);
                }
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                     // prepare defered glitch patching
                     terminal->glitch_cursor_x = event->cursor_position.x;
                     terminal->glitch_cursor_y = event->cursor_position.y;
                } else {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
                }
                terminal->ad_state = AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->seen_dec_terminal_param = true;
                // ignore
                return true;
            }
            break;
        case AD_FP1_QMCURSOR_POS_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                if (terminal->glitch_cursor_y != -1) {
                    termpaintp_patch_misparsing_defered(terminal, integration, AD_FINISHED);
                    return true;
                } else {
                    termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                    return true;
                }
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->seen_dec_terminal_param = true;
                if (terminal->auto_detect_sec_device_attributes.len
                        && termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT)
                        && termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS)
                        && termpaintp_str_ends_with(terminal->auto_detect_sec_device_attributes.data, (const uchar*)";0c")) {
                    terminal->terminal_type = TT_XTERM;
                }
                return true;
            }
            break;
        case AD_FP1_SEC_DEV_ATTRIB_QMCURSOR_POS_RECVED:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                int_puts(integration, "\033[>0;1c");
                int_puts(integration, "\033[5n");
                int_awaiting_response(integration);
                terminal->ad_state = AD_FP2_CURSOR_DONE;
                return true;
            } else if (event->type == TERMPAINT_EV_RAW_DECREQTPARM) {
                terminal->seen_dec_terminal_param = true;
                if (terminal->auto_detect_sec_device_attributes.len && event->raw.length == 4 && memcmp(event->raw.string, "\033[?x", 4) == 0
                        && terminal->glitch_cursor_y == -1) {
                    // this triggers on VTE < 0.54 which has fragile dictionary based parsing.
                    // The self reporting stage would cause misparsing, so skip it here.
                    terminal->terminal_type = TT_VTE;
                    terminal->ad_state = AD_EXPECT_SYNC_TO_FINISH;
                } else {
                    // ignore
                }
                return true;
            }
            break;
        case AD_FP2_REQ:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if (terminal->initial_cursor_y != event->cursor_position.y
                     || terminal->initial_cursor_x != event->cursor_position.x) {
                    // prepare defered glitch patching
                    terminal->glitch_cursor_x = event->cursor_position.x;
                    terminal->glitch_cursor_y = event->cursor_position.y;
                } else {
                    termpaint_terminal_promise_capability(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS);
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
                if (terminal->glitch_cursor_y == -1) {
                    termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                    return true;
                } else {
                    termpaintp_patch_misparsing_defered(terminal, integration, AD_FINISHED);
                    return true;
                }
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
                if (terminal->glitch_cursor_y == -1) {
                    termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                    return true;
                } else {
                    termpaintp_patch_misparsing_defered(terminal, integration, AD_FINISHED);
                    return true;
                }
            } else if (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB) {
                if (terminal->auto_detect_sec_device_attributes.len) {
                    terminal->terminal_type = TT_KONSOLE;
                } else if (terminal->terminal_type_confidence == 0) {
                    terminal->terminal_type = TT_BASE;
                }
                terminal->ad_state = AD_FP2_SEC_DEV_ATTRIB_RECVED2;
                return true;
            }
            break;
        case AD_FP2_SEC_DEV_ATTRIB_RECVED2:
            if (event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync()) {
                if (terminal->glitch_cursor_y == -1) {
                    termpaintp_terminal_auto_detect_prepare_self_reporting(terminal, AD_SELF_REPORTING);
                    return true;
                } else {
                    termpaintp_patch_misparsing_defered(terminal, integration, AD_FINISHED);
                    return true;
                }
            }
            break;
        // sub routine glitch patching
        case AD_GLITCH_PATCHING:
            if (event->type == TERMPAINT_EV_CURSOR_POSITION) {
                if ((event->cursor_position.y < terminal->glitch_cursor_y)
                        || ((event->cursor_position.y == terminal->glitch_cursor_y) && (event->cursor_position.x < terminal->glitch_cursor_x))) {
                    if (termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT)) {
                        int_puts(integration, " \033[?6n");
                    } else {
                        termpaint_input_expect_cursor_position_report(terminal->input);
                        int_puts(integration, " \033[6n");
                    }
                    return true;
                } else {
                    terminal->glitch_cursor_y = -1; // disarm
                    terminal->ad_state = terminal->glitch_patching_next_state;
                    if (terminal->glitch_patching_next_state == AD_FINISHED) {
                        return false;
                    } else {
                        BUG("AD_GLITCH_PATCHING called with destination state != AD_FINISHED");
                    }
                }
            }
            break;
        case AD_HTERM_RECOVERY1:
            if (event->type == TERMPAINT_EV_CHAR && event->c.length == 1
                       && event->c.string[0] == '0' && event->c.modifier == TERMPAINT_MOD_ALT) {
                terminal->ad_state = AD_HTERM_RECOVERY2;
                return true;
            } else if (event->type == TERMPAINT_EV_CHAR && event->c.length == 1
                       && event->c.string[0] == 'n' && event->c.modifier == 0) {
                // keep in recovery state.
                return true;
            } else if ((event->type == TERMPAINT_EV_CURSOR_POSITION)
                       || (event->type == TERMPAINT_EV_RAW_SEC_DEV_ATTRIB)) {
                // keep in recovery state.
                return true;
            }
            break;
        case AD_HTERM_RECOVERY2:
            if (event->type == TERMPAINT_EV_CHAR && event->c.length == 1
                       && event->c.string[0] == 'n' && event->c.modifier == 0) {
                terminal->ad_state = AD_FINISHED;
                return true;
            }
            break;
    };
    // if this code runs auto detection failed by running off into the weeds

    int_debuglog_printf(terminal, "ran off autodetect: s=%d, e=%d", (int)terminal->ad_state, (int)event->type);

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
    termpaintp_terminal_reset_capabilites(terminal);

    termpaintp_terminal_auto_detect_event(terminal, nullptr);
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

bool termpaint_terminal_might_be_supported(const termpaint_terminal *terminal) {
    return terminal->terminal_type != TT_INCOMPATIBLE;
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
        case TT_MLTERM:
            term_type = "mlterm";
            break;
        case TT_TERMINOLOGY:
            term_type = "terminology";
            break;
        case TT_MACOS:
            term_type = "apple terminal";
            break;
        case TT_ITERM2:
            term_type = "iterm2";
            break;
        case TT_MINTTY:
            term_type = "mintty";
            break;
        case TT_KITTY:
            term_type = "kitty";
            break;
        case TT_MSFT_TERMINAL:
            term_type = "microsoft terminal";
            break;
    };
    snprintf(buffer, buffer_length, "Type: %s(%d) %s seq:%s%s", term_type, terminal->terminal_version,
             termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT) ? "safe-CPR" : "",
             termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_CSI_GREATER) ? ">" : "",
             termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS) ? "=" : "");
    buffer[buffer_length-1] = 0;
}

const char *termpaint_terminal_self_reported_name_and_version(const termpaint_terminal *terminal) {
    return terminal->terminal_self_reported_name_version.len ?
                (const char*)terminal->terminal_self_reported_name_version.data
              : nullptr;
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

    termpaint_str *init_sequence = &terminal->unpause_basic_setup;

    termpaintp_prepend_str(&terminal->restore_seq, (const uchar*)"\033[?7h");
    terminal->did_terminal_disable_wrap = true;
    termpaintp_str_assign(init_sequence, "\033[?7l");

    if (!termpaintp_has_option(options, "-altscreen")) {
        termpaintp_prepend_str(&terminal->restore_seq, (const uchar*)"\r\n\033[?1049l");
        termpaintp_str_append(init_sequence, "\033[?1049h");
    }
    termpaintp_prepend_str(&terminal->restore_seq, (const uchar*)"\033[?66l");
    termpaintp_str_append(init_sequence, "\033[?66h");
    termpaintp_str_append(init_sequence, "\033[?1036h");
    if (!termpaintp_has_option(options, "+kbdsig") && terminal->terminal_type == TT_XTERM) {
        // xterm modify other characters
        // in this keyboard event mode xterm does no longer send the traditional one byte ^C, ^Z ^\ sequences
        // that the kernel tty layer uses to raise signals.
        termpaintp_prepend_str(&terminal->restore_seq, (const uchar*)"\033[>4m");
        termpaintp_str_append(init_sequence, "\033[>4;2m");
    }
    int_put_tps(integration, init_sequence);
    int_flush(integration);
    int_restore_sequence_updated(terminal);

    termpaint_surface_resize(&terminal->primary, width, height);
}

const char* termpaint_terminal_restore_sequence(const termpaint_terminal *term) {
    return (const char*)(term->restore_seq.len ? term->restore_seq.data : (const uchar*)"");
}

void termpaint_terminal_pause(termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;

    if (term->restore_seq.len) {
        int_write(integration, (const char*)term->restore_seq.data, term->restore_seq.len);
    }
    int_flush(integration);
}

void termpaint_terminal_unpause(termpaint_terminal *term) {
    term->cursor_prev_data = -2;
    termpaint_integration *integration = term->integration;

    // reconstruct state after setup_fullscreen
    int_put_tps(integration, &term->unpause_basic_setup);

    // save/push sequences
    if (term->did_terminal_push_title) {
        int_puts(integration, "\033[22t");
    }

    // other did_* sequences
    if (term->did_terminal_enable_mouse) {
        int_puts(integration, "\033[?1015h\033[?1006h");
    }

    // the rest
    for (int i = 0; i < term->colors.allocated; i++) {
        termpaint_color_entry* item_it = (termpaint_color_entry*)term->colors.buckets[i];
        while (item_it) {
            if (item_it->saved) {
                if (item_it->requested) {
                    int_puts(integration, "\033]");
                    int_uputs(integration, item_it->base.text);
                    int_puts(integration, ";");
                    int_uputs(integration, item_it->requested);
                    int_puts(integration, "\033\\");
                } else {
                    int_puts(integration, "\033]1");
                    int_uputs(integration, item_it->base.text);
                    int_puts(integration, "\033\\");
                }
            }
            item_it = (termpaint_color_entry*)item_it->base.next;
        }
    }

    for (int i = 0; i < term->unpause_snippets.allocated; i++) {
        termpaint_unpause_snippet* item_it = (termpaint_unpause_snippet*)term->unpause_snippets.buckets[i];
        while (item_it) {
            int_put_tps(integration, &item_it->sequences);
            item_it = (termpaint_unpause_snippet*)item_it->base.next;
        }
    }

    int_flush(integration);
}

static termpaint_str* termpaintp_terminal_get_unpause_slot(termpaint_terminal *term, const char *name) {
    termpaint_unpause_snippet *snippet = termpaintp_hash_ensure(&term->unpause_snippets,
                                                                (const unsigned char*)name);
    if (!snippet) {
        return nullptr;
    }
    return &(snippet)->sequences;
}

termpaint_attr *termpaint_attr_new_or_nullptr(unsigned fg, unsigned bg) {
    termpaint_attr *attr = calloc(1, sizeof(termpaint_attr));
    if (!attr) {
        return nullptr;
    }
    attr->fg_color = fg;
    attr->bg_color = bg;
    attr->deco_color = TERMPAINT_DEFAULT_COLOR;
    return attr;
}

termpaint_attr *termpaint_attr_new(unsigned fg, unsigned bg) {
    termpaint_attr *attr = termpaint_attr_new_or_nullptr(fg, bg);
    if (!attr) {
        termpaintp_oom_nolog();
    }
    return attr;
}

void termpaint_attr_free(termpaint_attr *attr) {
    if (!attr) {
        return;
    }

    free(attr->patch_setup);
    free(attr->patch_cleanup);
    free(attr);
}

termpaint_attr *termpaint_attr_clone_or_nullptr(const termpaint_attr *orig) {
    termpaint_attr *attr = calloc(1, sizeof(termpaint_attr));
    if (!attr) {
        return nullptr;
    }
    attr->fg_color = orig->fg_color;
    attr->bg_color = orig->bg_color;
    attr->deco_color = orig->deco_color;

    attr->flags = orig->flags;

    if (orig->patch_setup) {
        attr->patch_setup = ustrdup(orig->patch_setup);
        if (!attr->patch_setup) {
            termpaint_attr_free(attr);
            return nullptr;
        }
        attr->patch_optimize = orig->patch_optimize;
    }
    if (orig->patch_cleanup) {
        attr->patch_cleanup = ustrdup(orig->patch_cleanup);
        if (!attr->patch_cleanup) {
            termpaint_attr_free(attr);
            return nullptr;
        }
        attr->patch_optimize = orig->patch_optimize;
    }
    return attr;
}

termpaint_attr *termpaint_attr_clone(const termpaint_attr *orig) {
    termpaint_attr *attr = termpaint_attr_clone_or_nullptr(orig);
    if (!attr) {
        termpaintp_oom_nolog();
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

bool termpaint_attr_set_patch_mustcheck(termpaint_attr *attr, bool optimize, const char *setup, const char *cleanup) {
    free(attr->patch_setup);
    attr->patch_setup = nullptr;
    free(attr->patch_cleanup);
    attr->patch_cleanup = nullptr;
    if (!setup || !cleanup) {
        attr->patch_optimize = false;
    } else {
        attr->patch_optimize = optimize;
        attr->patch_setup = (uchar*)strdup(setup);
        if (!attr->patch_setup) {
            termpaint_attr_set_patch(attr, false, nullptr, nullptr);
            return false;
        }
        attr->patch_cleanup = (uchar*)strdup(cleanup);
        if (!attr->patch_cleanup) {
            termpaint_attr_set_patch(attr, false, nullptr, nullptr);
            return false;
        }
    }
    return true;
}

void termpaint_attr_set_patch(termpaint_attr *attr, bool optimize, const char *setup, const char *cleanup) {
    if (!termpaint_attr_set_patch_mustcheck(attr, optimize, setup, cleanup)) {
        termpaintp_oom_nolog();
    }
}

termpaint_text_measurement *termpaint_text_measurement_new_or_nullptr(const termpaint_surface *surface) {
    // Currently a fixed character classification table is used. But of course terminals differ
    // in character classification. Thus require a surface pointer already to later be able to
    // get to the terminal struct for details.
    // Pretend that we actually use surface here
    if (!surface) {
        BUG("termpaint_text_measurement_new called without valid surface");
    }
    termpaint_text_measurement *m = malloc(sizeof(termpaint_text_measurement));
    if (!m) {
        return nullptr;
    }
    termpaint_text_measurement_reset(m);
    return m;
}

termpaint_text_measurement *termpaint_text_measurement_new(const termpaint_surface *surface) {
    termpaint_text_measurement *m = termpaint_text_measurement_new_or_nullptr(surface);
    if (!m) {
        termpaintp_oom(surface->terminal);
    }
    return m;
}

void termpaint_text_measurement_free(termpaint_text_measurement *m) {
    if (!m) {
        return;
    }

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


int termpaint_text_measurement_pending_ref(const termpaint_text_measurement *m) {
    int result = m->pending_ref;
    if (m->decoder_state == TMD_PARTIAL_UTF16) {
        result += 1;
    } else if (m->decoder_state == TMD_PARTIAL_UTF8) {
        result += m->utf8_available;
    }
    return result;
}


int termpaint_text_measurement_last_codepoints(const termpaint_text_measurement *m) {
    return m->last_codepoints;
}

int termpaint_text_measurement_last_clusters(const termpaint_text_measurement *m) {
    return m->last_clusters;
}

int termpaint_text_measurement_last_width(const termpaint_text_measurement *m) {
    return m->last_width;
}

int termpaint_text_measurement_last_ref(const termpaint_text_measurement *m) {
    return m->last_ref;
}

int termpaint_text_measurement_limit_codepoints(const termpaint_text_measurement *m) {
    return m->limit_codepoints;
}

void termpaint_text_measurement_set_limit_codepoints(termpaint_text_measurement *m, int new_value) {
    m->limit_codepoints = new_value;
}

int termpaint_text_measurement_limit_clusters(const termpaint_text_measurement *m) {
    return m->limit_clusters;
}

void termpaint_text_measurement_set_limit_clusters(termpaint_text_measurement *m, int new_value) {
    m->limit_clusters = new_value;
}

int termpaint_text_measurement_limit_width(const termpaint_text_measurement *m) {
    return m->limit_width;
}

void termpaint_text_measurement_set_limit_width(termpaint_text_measurement *m, int new_value) {
    m->limit_width = new_value;
}

int termpaint_text_measurement_limit_ref(const termpaint_text_measurement *m) {
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
    int ch_sanitized = replace_unusable_codepoints(ch);
    int width = termpaintp_char_width(ch_sanitized);
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

            if (ch == '\x7f') {
                // clear marker does not allow any modifiers
                m->state = TM_INITIAL;
            }

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

bool termpaint_terminal_set_title_mustcheck(termpaint_terminal *term, const char *title, int mode) {
    if (mode != TERMPAINT_TITLE_MODE_PREFER_RESTORE) {
        if (!termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_TITLE_RESTORE)) {
            return true;
        }
    }

    termpaint_integration *integration = term->integration;

    if (!term->did_terminal_push_title) {
        termpaintp_prepend_str(&term->restore_seq, (const uchar*)"\033[23t");
        int_restore_sequence_updated(term);
        int_puts(integration, "\033[22t");
        term->did_terminal_push_title = true;
    }

    termpaint_str* sequences = termpaintp_terminal_get_unpause_slot(term, "title");
    if (!sequences) {
        return false;
    }

    TERMPAINT_STR_ASSIGN3_MUSTCHECK(sequences, S, "\033]2;", F, title, S, "\033\\");
    if (!sequences->len) {
        return false;
    }
    int_put_tps(integration, sequences);
    int_flush(integration);
    return true;
}

void termpaint_terminal_set_title(termpaint_terminal *term, const char *title, int mode) {
    if (!termpaint_terminal_set_title_mustcheck(term, title, mode)) {
        termpaintp_oom(term);
    }
}

bool termpaint_terminal_set_icon_title_mustcheck(termpaint_terminal *term, const char *title, int mode) {
    if (mode != TERMPAINT_TITLE_MODE_PREFER_RESTORE) {
        if (!termpaint_terminal_capable(term, TERMPAINT_CAPABILITY_TITLE_RESTORE)) {
            return true;
        }
    }

    termpaint_integration *integration = term->integration;

    if (!term->did_terminal_push_title) {
        termpaintp_prepend_str(&term->restore_seq, (const uchar*)"\033[23t");
        int_restore_sequence_updated(term);
        int_puts(integration, "\033[22t");
        term->did_terminal_push_title = true;
    }

    termpaint_str* sequences = termpaintp_terminal_get_unpause_slot(term, "icon title");
    if (!sequences) {
        return false;
    }

    TERMPAINT_STR_ASSIGN3_MUSTCHECK(sequences, S, "\033]1;", F, title, S, "\033\\");
    if (!sequences->len) {
        return false;
    }
    int_put_tps(integration, sequences);
    int_flush(integration);
    return true;
}

void termpaint_terminal_set_icon_title(termpaint_terminal *term, const char *title, int mode) {
    if (!termpaint_terminal_set_icon_title_mustcheck(term, title, mode)) {
        termpaintp_oom(term);
    }
}

void termpaint_terminal_bell(termpaint_terminal *term) {
    termpaint_integration *integration = term->integration;
    int_puts(integration, "\a");
    int_flush(integration);
}

#define DISABLE_MOUSE_SEQUENCE "\033[?1003l\033[?1002l\033[?1000l\033[?1006l\033[?1015l"

_Bool termpaint_terminal_set_mouse_mode_mustcheck(termpaint_terminal *term, int mouse_mode) {
    termpaint_integration *integration = term->integration;

    if (mouse_mode != TERMPAINT_MOUSE_MODE_OFF) {
        if (!term->did_terminal_add_mouse_to_restore) {
            termpaint_terminal_expect_legacy_mouse_reports(term, TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE);
            termpaintp_prepend_str(&term->restore_seq, (const uchar*)DISABLE_MOUSE_SEQUENCE);
            int_restore_sequence_updated(term);
            term->did_terminal_add_mouse_to_restore = true;
        }
    } else {
        if (term->did_terminal_enable_mouse) {
            term->did_terminal_enable_mouse = false;
            int_puts(integration, DISABLE_MOUSE_SEQUENCE);
            int_flush(integration);
            termpaint_str* sequences = termpaintp_terminal_get_unpause_slot(term, "mouse");
            if (sequences) {
                if (!termpaintp_str_assign_mustcheck(sequences, "")) {
                     return false;
                }
            }
        }
        return true;
    }
    termpaint_str* sequences = termpaintp_terminal_get_unpause_slot(term, "mouse");
    if (!sequences) {
        return false;
    }

    if (mouse_mode == TERMPAINT_MOUSE_MODE_CLICKS) {
        if (!termpaintp_str_assign_mustcheck(sequences, "\033[?1002l\033[?1003l\033[?1000h")) {
            return false;
        }
    } else if (mouse_mode == TERMPAINT_MOUSE_MODE_DRAG) {
        if (!termpaintp_str_assign_mustcheck(sequences, "\033[?1003l\033[?1000h\033[?1002h")) {
            return false;
        }
    } else if (mouse_mode == TERMPAINT_MOUSE_MODE_MOVEMENT) {
        if (!termpaintp_str_assign_mustcheck(sequences, "\033[?1000h\033[?1002h\033[?1003h")) {
            return false;
        }
    }

    if (!term->did_terminal_enable_mouse) {
        term->did_terminal_enable_mouse = true;
        int_puts(integration, "\033[?1015h\033[?1006h");
    }

    int_put_tps(integration, sequences);
    int_flush(integration);
    return true;
}

void termpaint_terminal_set_mouse_mode(termpaint_terminal *term, int mouse_mode) {
    if (!termpaint_terminal_set_mouse_mode_mustcheck(term, mouse_mode)) {
        termpaintp_oom(term);
    }
}

bool termpaint_terminal_request_focus_change_reports_mustcheck(termpaint_terminal *term, bool enabled) {
    if (enabled && !term->did_terminal_add_focusreporting_to_restore) {
        term->did_terminal_add_focusreporting_to_restore = true;
         termpaintp_prepend_str(&term->restore_seq, (const uchar*)"\033[?1004l");
         int_restore_sequence_updated(term);
    }

    termpaint_integration *integration = term->integration;

    termpaint_str* sequences = termpaintp_terminal_get_unpause_slot(term, "focus report");
    if (!sequences) {
        return false;
    }

    if (enabled) {
        if (!termpaintp_str_assign_mustcheck(sequences, "\033[?1004h")) {
            return false;
        }
    } else {
        if (!termpaintp_str_assign_mustcheck(sequences, "\033[?1004l")) {
            return false;
        }
    }
    int_put_tps(integration, sequences);
    int_flush(integration);
    return true;
}

void termpaint_terminal_request_focus_change_reports(termpaint_terminal *term, bool enabled) {
    if (!termpaint_terminal_request_focus_change_reports_mustcheck(term, enabled)) {
        termpaintp_oom(term);
    }
}

bool termpaint_terminal_request_tagged_paste_mustcheck(termpaint_terminal *term, bool enabled) {
    if (enabled && !term->did_terminal_add_bracketedpaste_to_restore) {
        term->did_terminal_add_bracketedpaste_to_restore = true;
         termpaintp_prepend_str(&term->restore_seq, (const uchar*)"\033[?2004l");
         int_restore_sequence_updated(term);
    }

    termpaint_integration *integration = term->integration;

    termpaint_str* sequences = termpaintp_terminal_get_unpause_slot(term, "bracketed paste");
    if (!sequences) {
        return false;
    }

    if (enabled) {
        if (!termpaintp_str_assign_mustcheck(sequences, "\033[?2004h")) {
            return false;
        }
    } else {
        if (!termpaintp_str_assign_mustcheck(sequences, "\033[?2004l")) {
            return false;
        }
    }
    int_put_tps(integration, sequences);
    int_flush(integration);
    return true;
}

void termpaint_terminal_request_tagged_paste(termpaint_terminal *term, bool enabled) {
    if (!termpaint_terminal_request_tagged_paste_mustcheck(term, enabled)) {
        termpaintp_oom(term);
    }
}

// --- tests

static bool termpaintp_test_quantize_to_256(void) {
    termpaint_terminal terminal;
    terminal.cache_should_use_truecolor = false;
    terminal.capabilities[TERMPAINT_CAPABILITY_88_COLOR] = false;
    struct pal_entry { int nr; int r,g,b; };
    const struct pal_entry palette[240] = {
        { 16, 0, 0, 0 }, { 17, 0, 0, 95 }, { 18, 0, 0, 135 }, { 19, 0, 0, 175 }, { 20, 0, 0, 215 },
        { 21, 0, 0, 255 }, { 22, 0, 95, 0 }, { 23, 0, 95, 95 }, { 24, 0, 95, 135 }, { 25, 0, 95, 175 },
        { 26, 0, 95, 215 }, { 27, 0, 95, 255 }, { 28, 0, 135, 0 }, { 29, 0, 135, 95 }, { 30, 0, 135, 135 },
        { 31, 0, 135, 175 }, { 32, 0, 135, 215 }, { 33, 0, 135, 255 }, { 34, 0, 175, 0 }, { 35, 0, 175, 95 },
        { 36, 0, 175, 135 }, { 37, 0, 175, 175 }, { 38, 0, 175, 215 }, { 39, 0, 175, 255 }, { 40, 0, 215, 0 },
        { 41, 0, 215, 95 }, { 42, 0, 215, 135 }, { 43, 0, 215, 175 }, { 44, 0, 215, 215 }, { 45, 0, 215, 255 },
        { 46, 0, 255, 0 }, { 47, 0, 255, 95 }, { 48, 0, 255, 135 }, { 49, 0, 255, 175 }, { 50, 0, 255, 215 },
        { 51, 0, 255, 255 }, { 52, 95, 0, 0 }, { 53, 95, 0, 95 }, { 54, 95, 0, 135 }, { 55, 95, 0, 175 },
        { 56, 95, 0, 215 }, { 57, 95, 0, 255 }, { 58, 95, 95, 0 }, { 59, 95, 95, 95 }, { 60, 95, 95, 135 },
        { 61, 95, 95, 175 }, { 62, 95, 95, 215 }, { 63, 95, 95, 255 }, { 64, 95, 135, 0 }, { 65, 95, 135, 95 },
        { 66, 95, 135, 135 }, { 67, 95, 135, 175 }, { 68, 95, 135, 215 }, { 69, 95, 135, 255 }, { 70, 95, 175, 0 },
        { 71, 95, 175, 95 }, { 72, 95, 175, 135 }, { 73, 95, 175, 175 }, { 74, 95, 175, 215 }, { 75, 95, 175, 255 },
        { 76, 95, 215, 0 }, { 77, 95, 215, 95 }, { 78, 95, 215, 135 }, { 79, 95, 215, 175 }, { 80, 95, 215, 215 },
        { 81, 95, 215, 255 }, { 82, 95, 255, 0 }, { 83, 95, 255, 95 }, { 84, 95, 255, 135 }, { 85, 95, 255, 175 },
        { 86, 95, 255, 215 }, { 87, 95, 255, 255 }, { 88, 135, 0, 0 }, { 89, 135, 0, 95 }, { 90, 135, 0, 135 },
        { 91, 135, 0, 175 }, { 92, 135, 0, 215 }, { 93, 135, 0, 255 }, { 94, 135, 95, 0 }, { 95, 135, 95, 95 },
        { 96, 135, 95, 135 }, { 97, 135, 95, 175 }, { 98, 135, 95, 215 }, { 99, 135, 95, 255 }, { 100, 135, 135, 0 },
        { 101, 135, 135, 95 }, { 102, 135, 135, 135 }, { 103, 135, 135, 175 }, { 104, 135, 135, 215 }, { 105, 135, 135, 255 },
        { 106, 135, 175, 0 }, { 107, 135, 175, 95 }, { 108, 135, 175, 135 }, { 109, 135, 175, 175 }, { 110, 135, 175, 215 },
        { 111, 135, 175, 255 }, { 112, 135, 215, 0 }, { 113, 135, 215, 95 }, { 114, 135, 215, 135 }, { 115, 135, 215, 175 },
        { 116, 135, 215, 215 }, { 117, 135, 215, 255 }, { 118, 135, 255, 0 }, { 119, 135, 255, 95 }, { 120, 135, 255, 135 },
        { 121, 135, 255, 175 }, { 122, 135, 255, 215 }, { 123, 135, 255, 255 }, { 124, 175, 0, 0 }, { 125, 175, 0, 95 },
        { 126, 175, 0, 135 }, { 127, 175, 0, 175 }, { 128, 175, 0, 215 }, { 129, 175, 0, 255 }, { 130, 175, 95, 0 },
        { 131, 175, 95, 95 }, { 132, 175, 95, 135 }, { 133, 175, 95, 175 }, { 134, 175, 95, 215 }, { 135, 175, 95, 255 },
        { 136, 175, 135, 0 }, { 137, 175, 135, 95 }, { 138, 175, 135, 135 }, { 139, 175, 135, 175 }, { 140, 175, 135, 215 },
        { 141, 175, 135, 255 }, { 142, 175, 175, 0 }, { 143, 175, 175, 95 }, { 144, 175, 175, 135 }, { 145, 175, 175, 175 },
        { 146, 175, 175, 215 }, { 147, 175, 175, 255 }, { 148, 175, 215, 0 }, { 149, 175, 215, 95 }, { 150, 175, 215, 135 },
        { 151, 175, 215, 175 }, { 152, 175, 215, 215 }, { 153, 175, 215, 255 }, { 154, 175, 255, 0 }, { 155, 175, 255, 95 },
        { 156, 175, 255, 135 }, { 157, 175, 255, 175 }, { 158, 175, 255, 215 }, { 159, 175, 255, 255 }, { 160, 215, 0, 0 },
        { 161, 215, 0, 95 }, { 162, 215, 0, 135 }, { 163, 215, 0, 175 }, { 164, 215, 0, 215 }, { 165, 215, 0, 255 },
        { 166, 215, 95, 0 }, { 167, 215, 95, 95 }, { 168, 215, 95, 135 }, { 169, 215, 95, 175 }, { 170, 215, 95, 215 },
        { 171, 215, 95, 255 }, { 172, 215, 135, 0 }, { 173, 215, 135, 95 }, { 174, 215, 135, 135 }, { 175, 215, 135, 175 },
        { 176, 215, 135, 215 }, { 177, 215, 135, 255 }, { 178, 215, 175, 0 }, { 179, 215, 175, 95 }, { 180, 215, 175, 135 },
        { 181, 215, 175, 175 }, { 182, 215, 175, 215 }, { 183, 215, 175, 255 }, { 184, 215, 215, 0 }, { 185, 215, 215, 95 },
        { 186, 215, 215, 135 }, { 187, 215, 215, 175 }, { 188, 215, 215, 215 }, { 189, 215, 215, 255 }, { 190, 215, 255, 0 },
        { 191, 215, 255, 95 }, { 192, 215, 255, 135 }, { 193, 215, 255, 175 }, { 194, 215, 255, 215 }, { 195, 215, 255, 255 },
        { 196, 255, 0, 0 }, { 197, 255, 0, 95 }, { 198, 255, 0, 135 }, { 199, 255, 0, 175 }, { 200, 255, 0, 215 }, { 201, 255, 0, 255 },
        { 202, 255, 95, 0 }, { 203, 255, 95, 95 }, { 204, 255, 95, 135 }, { 205, 255, 95, 175 }, { 206, 255, 95, 215 },
        { 207, 255, 95, 255 }, { 208, 255, 135, 0 }, { 209, 255, 135, 95 }, { 210, 255, 135, 135 }, { 211, 255, 135, 175 },
        { 212, 255, 135, 215 }, { 213, 255, 135, 255 }, { 214, 255, 175, 0 }, { 215, 255, 175, 95 }, { 216, 255, 175, 135 },
        { 217, 255, 175, 175 }, { 218, 255, 175, 215 }, { 219, 255, 175, 255 }, { 220, 255, 215, 0 }, { 221, 255, 215, 95 },
        { 222, 255, 215, 135 }, { 223, 255, 215, 175 }, { 224, 255, 215, 215 }, { 225, 255, 215, 255 }, { 226, 255, 255, 0 },
        { 227, 255, 255, 95 }, { 228, 255, 255, 135 }, { 229, 255, 255, 175 }, { 230, 255, 255, 215 }, { 231, 255, 255, 255 },

        { 232, 8, 8, 8 }, { 233, 18, 18, 18 }, { 234, 28, 28, 28 }, { 235, 38, 38, 38 }, { 236, 48, 48, 48 },
        { 237, 58, 58, 58 }, { 238, 68, 68, 68 }, { 239, 78, 78, 78 }, { 240, 88, 88, 88 }, { 241, 98, 98, 98 },
        { 242, 108, 108, 108 }, { 243, 118, 118, 118 }, { 244, 128, 128, 128 }, { 245, 138, 138, 138 },
        { 246, 148, 148, 148 }, { 247, 158, 158, 158 }, { 248, 168, 168, 168 }, { 249, 178, 178, 178 },
        { 250, 188, 188, 188 }, { 251, 198, 198, 198 }, { 252, 208, 208, 208 }, { 253, 218, 218, 218 },
        { 254, 228, 228, 228 }, { 255, 238, 238, 238 },
    };

    for (int r = 0; r < 256; r++) for (int g = 0; g < 256; g++) for (int b = 0; b < 256; b++) {
        int best_metric = 0x7fffffff;

        unsigned candidates[10] = { 0 };
        int candidates_count = 0;

        for (int i = 0; i < 240; i++) {
#define SQ(x) ((x) * (x))
            const int cur_metric = SQ(palette[i].r - r) + SQ(palette[i].g - g) + SQ(palette[i].b - b);
#undef SQ
            if (cur_metric < best_metric) {
                candidates_count = 0;
                candidates[candidates_count++] = TERMPAINT_INDEXED_COLOR + palette[i].nr;
                best_metric = cur_metric;
            } else if (cur_metric == best_metric) {
                candidates[candidates_count++] = TERMPAINT_INDEXED_COLOR + palette[i].nr;
            }
        }
        if (candidates_count == 9) {
            return false;
        }

        unsigned res = termpaintp_quantize_color(&terminal, TERMPAINT_RGB_COLOR(r, g, b));

        bool ok = false;
        for (int i = 0; i < candidates_count; i++) {
            if (candidates[i] == res) {
                ok = true;
                break;
            }
        }

        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool termpaintp_test_quantize_to_88(void) {
    termpaint_terminal terminal;
    terminal.cache_should_use_truecolor = false;
    terminal.capabilities[TERMPAINT_CAPABILITY_88_COLOR] = true;
    struct pal_entry { int nr; int r,g,b; };
    const struct pal_entry palette[72] = {
        { 16, 0x00, 0x00, 0x00 }, { 17, 0x00, 0x00, 0x8b }, { 18, 0x00, 0x00, 0xcd }, { 19, 0x00, 0x00, 0xff },
        { 20, 0x00, 0x8b, 0x00 }, { 21, 0x00, 0x8b, 0x8b }, { 22, 0x00, 0x8b, 0xcd }, { 23, 0x00, 0x8b, 0xff },
        { 24, 0x00, 0xcd, 0x00 }, { 25, 0x00, 0xcd, 0x8b }, { 26, 0x00, 0xcd, 0xcd }, { 27, 0x00, 0xcd, 0xff },
        { 28, 0x00, 0xff, 0x00 }, { 29, 0x00, 0xff, 0x8b }, { 30, 0x00, 0xff, 0xcd }, { 31, 0x00, 0xff, 0xff },
        { 32, 0x8b, 0x00, 0x00 }, { 33, 0x8b, 0x00, 0x8b }, { 34, 0x8b, 0x00, 0xcd }, { 35, 0x8b, 0x00, 0xff },
        { 36, 0x8b, 0x8b, 0x00 }, { 37, 0x8b, 0x8b, 0x8b }, { 38, 0x8b, 0x8b, 0xcd }, { 39, 0x8b, 0x8b, 0xff },
        { 40, 0x8b, 0xcd, 0x00 }, { 41, 0x8b, 0xcd, 0x8b }, { 42, 0x8b, 0xcd, 0xcd }, { 43, 0x8b, 0xcd, 0xff },
        { 44, 0x8b, 0xff, 0x00 }, { 45, 0x8b, 0xff, 0x8b }, { 46, 0x8b, 0xff, 0xcd }, { 47, 0x8b, 0xff, 0xff },
        { 48, 0xcd, 0x00, 0x00 }, { 49, 0xcd, 0x00, 0x8b }, { 50, 0xcd, 0x00, 0xcd }, { 51, 0xcd, 0x00, 0xff },
        { 52, 0xcd, 0x8b, 0x00 }, { 53, 0xcd, 0x8b, 0x8b }, { 54, 0xcd, 0x8b, 0xcd }, { 55, 0xcd, 0x8b, 0xff },
        { 56, 0xcd, 0xcd, 0x00 }, { 57, 0xcd, 0xcd, 0x8b }, { 58, 0xcd, 0xcd, 0xcd }, { 59, 0xcd, 0xcd, 0xff },
        { 60, 0xcd, 0xff, 0x00 }, { 61, 0xcd, 0xff, 0x8b }, { 62, 0xcd, 0xff, 0xcd }, { 63, 0xcd, 0xff, 0xff },
        { 64, 0xff, 0x00, 0x00 }, { 65, 0xff, 0x00, 0x8b }, { 66, 0xff, 0x00, 0xcd }, { 67, 0xff, 0x00, 0xff },
        { 68, 0xff, 0x8b, 0x00 }, { 69, 0xff, 0x8b, 0x8b }, { 70, 0xff, 0x8b, 0xcd }, { 71, 0xff, 0x8b, 0xff },
        { 72, 0xff, 0xcd, 0x00 }, { 73, 0xff, 0xcd, 0x8b }, { 74, 0xff, 0xcd, 0xcd }, { 75, 0xff, 0xcd, 0xff },
        { 76, 0xff, 0xff, 0x00 }, { 77, 0xff, 0xff, 0x8b }, { 78, 0xff, 0xff, 0xcd }, { 79, 0xff, 0xff, 0xff },

        { 80, 0x2e, 0x2e, 0x2e }, { 81, 0x5c, 0x5c, 0x5c }, { 82, 0x73, 0x73, 0x73 }, { 83, 0x8b, 0x8b, 0x8b },
        { 84, 0xa2, 0xa2, 0xa2 }, { 85, 0xb9, 0xb9, 0xb9 }, { 86, 0xd0, 0xd0, 0xd0 }, { 87, 0xe7, 0xe7, 0xe7 },
    };

    for (int r = 0; r < 256; r++) for (int g = 0; g < 256; g++) for (int b = 0; b < 256; b++) {
        int best_metric = 0x7fffffff;

        unsigned candidates[10] = { 0 };
        int candidates_count = 0;

        for (int i = 0; i < 72; i++) {
#define SQ(x) ((x) * (x))
            const int cur_metric = SQ(palette[i].r - r) + SQ(palette[i].g - g) + SQ(palette[i].b - b);
#undef SQ
            if (cur_metric < best_metric) {
                candidates_count = 0;
                candidates[candidates_count++] = TERMPAINT_INDEXED_COLOR + palette[i].nr;
                best_metric = cur_metric;
            } else if (cur_metric == best_metric) {
                candidates[candidates_count++] = TERMPAINT_INDEXED_COLOR + palette[i].nr;
            }
        }
        if (candidates_count == 9) {
            return false;
        }

        unsigned res = termpaintp_quantize_color(&terminal, TERMPAINT_RGB_COLOR(r, g, b));

        bool ok = false;
        for (int i = 0; i < candidates_count; i++) {
            if (candidates[i] == res) {
                ok = true;
                break;
            }
        }

        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool termpaintp_test_parse_version(void) {
    bool ret = true;
    ret &= (termpaintp_parse_version("0.5.0") == 5000);
    ret &= (termpaintp_parse_version("0.5.1") == 5001);
    ret &= (termpaintp_parse_version("1") == 1000000);
    ret &= (termpaintp_parse_version("1.0") == 1000000);
    ret &= (termpaintp_parse_version("1.0.0") == 1000000);
    ret &= (termpaintp_parse_version("1.7") == 1007000);
    ret &= (termpaintp_parse_version("1.7.0") == 1007000);
    ret &= (termpaintp_parse_version("1.7.0a") == 1007000);
    ret &= (termpaintp_parse_version("1.7a.0") == 1007000);
    ret &= (termpaintp_parse_version("1.7.0.1") == 1007000);
    ret &= (termpaintp_parse_version("1.7.1") == 1007001);
    ret &= (termpaintp_parse_version("1.7.1a") == 1007001);
    ret &= (termpaintp_parse_version("1.7a.1") == 1007000);
    return ret;
}

_tERMPAINT_PUBLIC bool termpaintp_test(void) {
    bool ret = true;
    ret &= termpaintp_test_quantize_to_256();
    ret &= termpaintp_test_quantize_to_88();
    ret &= termpaintp_test_parse_version();
    ret &= termpaintp_mem_ascii_case_insensitive_equals("A", "a", 1);
    ret &= !termpaintp_mem_ascii_case_insensitive_equals("[", "{", 1);
    return ret;
}
