#ifndef TERMPAINT_TERMPAINT_INCLUDED
#define TERMPAINT_TERMPAINT_INCLUDED

#include <stdint.h>

#include <termpaint_event.h>

#ifdef __cplusplus
#ifndef _Bool
#define _Bool bool
#endif
#endif

#ifdef __cplusplus
extern "C" {
#define TERMPAINTP_CAST(TYPE, EXPR) static_cast<TYPE>(EXPR)
#else
#define TERMPAINTP_CAST(TYPE, EXPR) (TYPE)(EXPR)
#endif

struct termpaint_attr_;
typedef struct termpaint_attr_ termpaint_attr;

struct termpaint_text_measurement_;
typedef struct termpaint_text_measurement_ termpaint_text_measurement;

struct termpaint_surface_;
typedef struct termpaint_surface_ termpaint_surface;

struct termpaint_terminal_;
typedef struct termpaint_terminal_ termpaint_terminal;

typedef struct termpaint_integration_ {
    void (*free)(struct termpaint_integration_ *integration);
    void (*write)(struct termpaint_integration_ *integration, char *data, int length);
    void (*flush)(struct termpaint_integration_ *integration);
    _Bool (*is_bad)(struct termpaint_integration_ *integration);
    void (*request_callback)(struct termpaint_integration_ *integration);
} termpaint_integration;

_tERMPAINT_PUBLIC termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration);
_tERMPAINT_PUBLIC void termpaint_terminal_free(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_free_with_restore(termpaint_terminal *term);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_flush(termpaint_terminal *term, _Bool full_repaint);
_tERMPAINT_PUBLIC const char *termpaint_terminal_restore_sequence(const termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_reset_attributes(const termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_set_cursor(termpaint_terminal *term, int x, int y);
_tERMPAINT_PUBLIC void termpaint_terminal_set_cursor_position(termpaint_terminal *term, int x, int y);
_tERMPAINT_PUBLIC void termpaint_terminal_set_cursor_visible(termpaint_terminal *term, _Bool visible);

#define TERMPAINT_CURSOR_STYLE_TERM_DEFAULT 0
#define TERMPAINT_CURSOR_STYLE_BLOCK 1
#define TERMPAINT_CURSOR_STYLE_UNDERLINE 3
#define TERMPAINT_CURSOR_STYLE_BAR 5

_tERMPAINT_PUBLIC void termpaint_terminal_set_cursor_style(termpaint_terminal *term, int style, _Bool blink);

_tERMPAINT_PUBLIC void termpaint_terminal_callback(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_set_raw_input_filter_cb(termpaint_terminal *term, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data);
_tERMPAINT_PUBLIC void termpaint_terminal_set_event_cb(termpaint_terminal *term, void (*cb)(void *user_data, termpaint_event* event), void *user_data);
_tERMPAINT_PUBLIC void termpaint_terminal_add_input_data(termpaint_terminal *term, const char *data, unsigned length);
_tERMPAINT_PUBLIC const char* termpaint_terminal_peek_input_buffer(const termpaint_terminal *term);
_tERMPAINT_PUBLIC int termpaint_terminal_peek_input_buffer_length(const termpaint_terminal *term);

_tERMPAINT_PUBLIC _Bool termpaint_terminal_auto_detect(termpaint_terminal *terminal);
enum termpaint_auto_detect_state_enum { termpaint_auto_detect_none,
                                                 termpaint_auto_detect_running,
                                                 termpaint_auto_detect_done };
_tERMPAINT_PUBLIC enum termpaint_auto_detect_state_enum termpaint_terminal_auto_detect_state(const termpaint_terminal *terminal);
_tERMPAINT_PUBLIC void termpaint_terminal_auto_detect_result_text(const termpaint_terminal *terminal, char *buffer, int buffer_length);
_tERMPAINT_PUBLIC void termpaint_terminal_setup_fullscreen(termpaint_terminal *terminal, int width, int height, const char *options);

_tERMPAINT_PUBLIC termpaint_attr* termpaint_attr_new(int fg, int bg);
_tERMPAINT_PUBLIC termpaint_attr* termpaint_attr_clone(termpaint_attr* attr);
_tERMPAINT_PUBLIC void termpaint_attr_free(termpaint_attr* attr);
_tERMPAINT_PUBLIC void termpaint_attr_set_fg(termpaint_attr* attr, int fg);
_tERMPAINT_PUBLIC void termpaint_attr_set_bg(termpaint_attr* attr, int bg);
_tERMPAINT_PUBLIC void termpaint_attr_set_deco(termpaint_attr* attr, int deco_color);
_tERMPAINT_PUBLIC void termpaint_attr_set_patch(termpaint_attr* attr, _Bool optimize, const char *setup, const char * cleanup);
#define TERMPAINT_DEFAULT_COLOR 0x1000000
#define TERMPAINT_NAMED_COLOR 0x1100000
#define TERMPAINT_INDEXED_COLOR 0x1200000
#define TERMPAINT_RGB_COLOR(r, g, b) ((TERMPAINTP_CAST(unsigned,r) << 16) | (TERMPAINTP_CAST(unsigned, g) << 8) | TERMPAINTP_CAST(unsigned, b))

#define TERMPAINT_STYLE_BOLD (1<<0)
#define TERMPAINT_STYLE_ITALIC (1<<1)
#define TERMPAINT_STYLE_BLINK (1<<4)
#define TERMPAINT_STYLE_OVERLINE (1<<5)
#define TERMPAINT_STYLE_INVERSE (1<<6)
#define TERMPAINT_STYLE_STRIKE (1<<7)
#define TERMPAINT_STYLE_UNDERLINE (1<<16)
#define TERMPAINT_STYLE_UNDERLINE_DBL (1<<17)
#define TERMPAINT_STYLE_UNDERLINE_CURLY (1<<18)
_tERMPAINT_PUBLIC void termpaint_attr_set_style(termpaint_attr* attr, int bits);
_tERMPAINT_PUBLIC void termpaint_attr_unset_style(termpaint_attr* attr, int bits);
_tERMPAINT_PUBLIC void termpaint_attr_reset_style(termpaint_attr* attr);

_tERMPAINT_PUBLIC termpaint_surface *termpaint_terminal_new_surface(termpaint_terminal *term, int width, int height);
_tERMPAINT_PUBLIC void termpaint_surface_free(termpaint_surface *surface);
_tERMPAINT_PUBLIC void termpaint_surface_resize(termpaint_surface *surface, int width, int height);
_tERMPAINT_PUBLIC int termpaint_surface_width(const termpaint_surface *surface);
_tERMPAINT_PUBLIC int termpaint_surface_height(const termpaint_surface *surface);
_tERMPAINT_PUBLIC int termpaint_surface_char_width(const termpaint_surface *surface, int codepoint);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_attr(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_attr_clipped(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr, int clip_x0, int clip_x1);
_tERMPAINT_PUBLIC void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg);
_tERMPAINT_PUBLIC void termpaint_surface_clear_with_attr(termpaint_surface *surface, const termpaint_attr *attr);
_tERMPAINT_PUBLIC void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg);
_tERMPAINT_PUBLIC void termpaint_surface_clear_rect_with_attr(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr);
#define TERMPAINT_COPY_NO_TILE 0
#define TERMPAINT_COPY_TILE_PRESERVE -1
#define TERMPAINT_COPY_TILE_PUT 1
_tERMPAINT_PUBLIC void termpaint_surface_copy_rect(termpaint_surface *src_surface, int x, int y, int width, int height,
                                 termpaint_surface *dst_surface, int dst_x, int dst_y,
                                 int tile_left, int tile_right);
_tERMPAINT_PUBLIC void termpaint_surface_tint(termpaint_surface *surface,
                            void (*recolor)(void *user_data, unsigned *fg, unsigned *bg, unsigned *deco),
                            void *user_data);

_tERMPAINT_PUBLIC unsigned termpaint_surface_peek_fg_color(const termpaint_surface *surface, int x, int y);
_tERMPAINT_PUBLIC unsigned termpaint_surface_peek_bg_color(const termpaint_surface *surface, int x, int y);
_tERMPAINT_PUBLIC unsigned termpaint_surface_peek_deco_color(const termpaint_surface *surface, int x, int y);
_tERMPAINT_PUBLIC int termpaint_surface_peek_style(const termpaint_surface *surface, int x, int y);
_tERMPAINT_PUBLIC void termpaint_surface_peek_patch(const termpaint_surface *surface, int x, int y, const char **setup, const char **cleanup, _Bool *optimize);
_tERMPAINT_PUBLIC const char *termpaint_surface_peek_text(const termpaint_surface *surface, int x, int y, int *len, int *left, int *right);
_tERMPAINT_PUBLIC _Bool termpaint_surface_same_contents(const termpaint_surface *surface1, const termpaint_surface *surface2);

_tERMPAINT_PUBLIC termpaint_text_measurement* termpaint_text_measurement_new(termpaint_surface *surface);
_tERMPAINT_PUBLIC void termpaint_text_measurement_free(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_reset(termpaint_text_measurement *m);

_tERMPAINT_PUBLIC int termpaint_text_measurement_pending_ref(termpaint_text_measurement *m);

_tERMPAINT_PUBLIC int termpaint_text_measurement_last_codepoints(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC int termpaint_text_measurement_last_clusters(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC int termpaint_text_measurement_last_width(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC int termpaint_text_measurement_last_ref(termpaint_text_measurement *m);

_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_codepoints(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_codepoints(termpaint_text_measurement *m, int new_value);
_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_clusters(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_clusters(termpaint_text_measurement *m, int new_value);
_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_width(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_width(termpaint_text_measurement *m, int new_value);
_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_ref(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_ref(termpaint_text_measurement *m, int new_value);

_tERMPAINT_PUBLIC _Bool /* new cluster*/ termpaint_text_measurement_feed_codepoint(termpaint_text_measurement *m, int ch, int ref_adjust);
_tERMPAINT_PUBLIC _Bool /* reached limit */ termpaint_text_measurement_feed_utf32(termpaint_text_measurement *m, const uint32_t *chars, int length, _Bool final);
_tERMPAINT_PUBLIC _Bool /* reached limit */ termpaint_text_measurement_feed_utf16(termpaint_text_measurement *m, const uint16_t *code_units, int length, _Bool final);
_tERMPAINT_PUBLIC _Bool /* reached limit */ termpaint_text_measurement_feed_utf8(termpaint_text_measurement *m, const uint8_t *code_units, int length, _Bool final);

#ifdef __cplusplus
}
#endif

#endif
