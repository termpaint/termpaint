// SPDX-License-Identifier: BSL-1.0
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


struct termpaint_integration_private_;
typedef struct termpaint_integration_private_ termpaint_integration_private;


typedef struct termpaint_integration_ {
    termpaint_integration_private* p;
} termpaint_integration;

_tERMPAINT_PUBLIC void termpaint_integration_init(termpaint_integration *integration,
                                                  void (*free)(termpaint_integration *integration),
                                                  void (*write)(termpaint_integration *integration, const char *data, int length),
                                                  void (*flush)(termpaint_integration *integration));
_tERMPAINT_PUBLIC _Bool termpaint_integration_init_mustcheck(termpaint_integration *integration,
                                                             void (*free)(termpaint_integration *integration),
                                                             void (*write)(termpaint_integration *integration, const char *data, int length),
                                                             void (*flush)(termpaint_integration *integration));
_tERMPAINT_PUBLIC void termpaint_integration_deinit(termpaint_integration *integration);
_tERMPAINT_PUBLIC void termpaint_integration_set_is_bad(termpaint_integration *integration, _Bool (*is_bad)(termpaint_integration *integration));
_tERMPAINT_PUBLIC void termpaint_integration_set_request_callback(termpaint_integration *integration, void (*request_callback)(termpaint_integration *integration));
_tERMPAINT_PUBLIC void termpaint_integration_set_awaiting_response(termpaint_integration *integration, void (*awaiting_response)(termpaint_integration *integration));
_tERMPAINT_PUBLIC void termpaint_integration_set_restore_sequence_updated(termpaint_integration *integration, void (*restore_sequence_updated)(termpaint_integration *integration, const char *data, int length));
_tERMPAINT_PUBLIC void termpaint_integration_set_logging_func(termpaint_integration *integration, void (*logging_func)(termpaint_integration *integration, const char *data, int length));

// getters go here if need arises

_tERMPAINT_PUBLIC termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration);
_tERMPAINT_PUBLIC termpaint_terminal *termpaint_terminal_new_or_nullptr(termpaint_integration *integration);
_tERMPAINT_PUBLIC void termpaint_terminal_free(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_free_with_restore(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_free_with_restore_and_persistent(termpaint_terminal *term, termpaint_surface *surface);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_flush(termpaint_terminal *term, _Bool full_repaint);
_tERMPAINT_PUBLIC const char *termpaint_terminal_restore_sequence(const termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_set_cursor_position(termpaint_terminal *term, int x, int y);
_tERMPAINT_PUBLIC void termpaint_terminal_set_cursor_visible(termpaint_terminal *term, _Bool visible);

#define TERMPAINT_CURSOR_STYLE_TERM_DEFAULT 0
#define TERMPAINT_CURSOR_STYLE_BLOCK 1
#define TERMPAINT_CURSOR_STYLE_UNDERLINE 3
#define TERMPAINT_CURSOR_STYLE_BAR 5

_tERMPAINT_PUBLIC void termpaint_terminal_set_cursor_style(termpaint_terminal *term, int style, _Bool blink);

#define TERMPAINT_COLOR_SLOT_FOREGRUND 10
#define TERMPAINT_COLOR_SLOT_BACKGROUND 11
#define TERMPAINT_COLOR_SLOT_CURSOR 12

_tERMPAINT_PUBLIC _Bool termpaint_terminal_set_color_mustcheck(termpaint_terminal *term, int color_slot, int r, int g, int b);
_tERMPAINT_PUBLIC void termpaint_terminal_set_color(termpaint_terminal *term, int color_slot, int r, int g, int b);
_tERMPAINT_PUBLIC void termpaint_terminal_reset_color(termpaint_terminal *term, int color_slot);

#define TERMPAINT_TITLE_MODE_ENSURE_RESTORE 0
#define TERMPAINT_TITLE_MODE_PREFER_RESTORE 1

_tERMPAINT_PUBLIC void termpaint_terminal_set_title(termpaint_terminal *term, const char* title, int mode);
_tERMPAINT_PUBLIC _Bool termpaint_terminal_set_title_mustcheck(termpaint_terminal *term, const char* title, int mode);
_tERMPAINT_PUBLIC void termpaint_terminal_set_icon_title(termpaint_terminal *term, const char* title, int mode);
_tERMPAINT_PUBLIC _Bool termpaint_terminal_set_icon_title_mustcheck(termpaint_terminal *term, const char* title, int mode);

_tERMPAINT_PUBLIC void termpaint_terminal_bell(termpaint_terminal *term);

#define TERMPAINT_MOUSE_MODE_OFF 0
#define TERMPAINT_MOUSE_MODE_CLICKS 1
#define TERMPAINT_MOUSE_MODE_DRAG 2
#define TERMPAINT_MOUSE_MODE_MOVEMENT 3

_tERMPAINT_PUBLIC void termpaint_terminal_set_mouse_mode(termpaint_terminal *term, int mouse_mode);
_tERMPAINT_PUBLIC _Bool termpaint_terminal_set_mouse_mode_mustcheck(termpaint_terminal *term, int mouse_mode);

_tERMPAINT_PUBLIC void termpaint_terminal_request_focus_change_reports(termpaint_terminal *term, _Bool enabled);
_tERMPAINT_PUBLIC _Bool termpaint_terminal_request_focus_change_reports_mustcheck(termpaint_terminal *term, _Bool enabled);
_tERMPAINT_PUBLIC void termpaint_terminal_request_tagged_paste(termpaint_terminal *term, _Bool enabled);
_tERMPAINT_PUBLIC _Bool termpaint_terminal_request_tagged_paste_mustcheck(termpaint_terminal *term, _Bool enabled);

_tERMPAINT_PUBLIC void termpaint_terminal_callback(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_set_raw_input_filter_cb(termpaint_terminal *term, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data);
_tERMPAINT_PUBLIC void termpaint_terminal_set_event_cb(termpaint_terminal *term, void (*cb)(void *user_data, termpaint_event* event), void *user_data);
_tERMPAINT_PUBLIC void termpaint_terminal_add_input_data(termpaint_terminal *term, const char *data, unsigned length);
_tERMPAINT_PUBLIC const char* termpaint_terminal_peek_input_buffer(const termpaint_terminal *term);
_tERMPAINT_PUBLIC int termpaint_terminal_peek_input_buffer_length(const termpaint_terminal *term);

// wrapped input option/state setters
_tERMPAINT_PUBLIC void termpaint_terminal_expect_cursor_position_report(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_expect_legacy_mouse_reports(termpaint_terminal *term, int s);
_tERMPAINT_PUBLIC void termpaint_terminal_handle_paste(termpaint_terminal *term, _Bool enabled);
_tERMPAINT_PUBLIC void termpaint_terminal_expect_apc_input_sequences(termpaint_terminal *term, _Bool enabled);
_tERMPAINT_PUBLIC void termpaint_terminal_activate_input_quirk(termpaint_terminal *term, int quirk);

_tERMPAINT_PUBLIC _Bool termpaint_terminal_auto_detect(termpaint_terminal *terminal);
enum termpaint_auto_detect_state_enum { termpaint_auto_detect_none,
                                                 termpaint_auto_detect_running,
                                                 termpaint_auto_detect_done };
_tERMPAINT_PUBLIC enum termpaint_auto_detect_state_enum termpaint_terminal_auto_detect_state(const termpaint_terminal *terminal);
_tERMPAINT_PUBLIC _Bool termpaint_terminal_might_be_supported(const termpaint_terminal *terminal);
_tERMPAINT_PUBLIC void termpaint_terminal_auto_detect_apply_input_quirks(termpaint_terminal *terminal, _Bool backspace_is_x08);
_tERMPAINT_PUBLIC void termpaint_terminal_auto_detect_result_text(const termpaint_terminal *terminal, char *buffer, int buffer_length);
_tERMPAINT_PUBLIC const char *termpaint_terminal_self_reported_name_and_version(const termpaint_terminal *terminal);
_tERMPAINT_PUBLIC void termpaint_terminal_setup_fullscreen(termpaint_terminal *terminal, int width, int height, const char *options);
_tERMPAINT_PUBLIC void termpaint_terminal_setup_inline(termpaint_terminal *terminal, int width, int height, const char *options);
_tERMPAINT_PUBLIC void termpaint_terminal_set_inline(termpaint_terminal *terminal, _Bool enabled);

#define TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT 0
#define TERMPAINT_CAPABILITY_CSI_GREATER 1
#define TERMPAINT_CAPABILITY_CSI_EQUALS 2
#define TERMPAINT_CAPABILITY_CSI_POSTFIX_MOD 3
#define TERMPAINT_CAPABILITY_TITLE_RESTORE 4
#define TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE_BAR 5
#define TERMPAINT_CAPABILITY_CURSOR_SHAPE_OSC50 6
#define TERMPAINT_CAPABILITY_EXTENDED_CHARSET 7
#define TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED 8
#define TERMPAINT_CAPABILITY_TRUECOLOR_SUPPORTED 9
#define TERMPAINT_CAPABILITY_88_COLOR 10
#define TERMPAINT_CAPABILITY_CLEARED_COLORING 11
#define TERMPAINT_CAPABILITY_7BIT_ST 12
#define TERMPAINT_CAPABILITY_MAY_TRY_CURSOR_SHAPE 13
#define TERMPAINT_CAPABILITY_MAY_TRY_TAGGED_PASTE 14
#define TERMPAINT_CAPABILITY_CLEARED_COLORING_DEFCOLOR 15

_tERMPAINT_PUBLIC _Bool termpaint_terminal_capable(const termpaint_terminal *terminal, int capability);
_tERMPAINT_PUBLIC void termpaint_terminal_promise_capability(termpaint_terminal *terminal, int capability);
_tERMPAINT_PUBLIC void termpaint_terminal_disable_capability(termpaint_terminal *terminal, int capability);
_tERMPAINT_PUBLIC _Bool termpaint_terminal_should_use_truecolor(termpaint_terminal *terminal);

_tERMPAINT_PUBLIC void termpaint_terminal_pause(termpaint_terminal *term);
_tERMPAINT_PUBLIC void termpaint_terminal_pause_and_persistent(termpaint_terminal *term, termpaint_surface *surface);
_tERMPAINT_PUBLIC void termpaint_terminal_unpause(termpaint_terminal *term);

#define TERMPAINT_LOG_AUTO_DETECT_TRACE (1 << 0)
#define TERMPAINT_LOG_TRACE_RAW_INPUT (1 << 1)

_tERMPAINT_PUBLIC void termpaint_terminal_set_log_mask(termpaint_terminal *term, unsigned mask);
_tERMPAINT_PUBLIC void termpaint_terminal_glitch_on_out_of_memory(termpaint_terminal *term);

_tERMPAINT_PUBLIC termpaint_attr* termpaint_attr_new(unsigned fg, unsigned bg);
_tERMPAINT_PUBLIC termpaint_attr* termpaint_attr_new_or_nullptr(unsigned fg, unsigned bg);
_tERMPAINT_PUBLIC termpaint_attr* termpaint_attr_clone(const termpaint_attr* attr);
_tERMPAINT_PUBLIC termpaint_attr* termpaint_attr_clone_or_nullptr(const termpaint_attr* attr);
_tERMPAINT_PUBLIC void termpaint_attr_free(termpaint_attr* attr);
_tERMPAINT_PUBLIC void termpaint_attr_set_fg(termpaint_attr* attr, unsigned fg);
_tERMPAINT_PUBLIC void termpaint_attr_set_bg(termpaint_attr* attr, unsigned bg);
_tERMPAINT_PUBLIC void termpaint_attr_set_deco(termpaint_attr* attr, unsigned deco_color);
_tERMPAINT_PUBLIC void termpaint_attr_set_patch(termpaint_attr* attr, _Bool optimize, const char *setup, const char * cleanup);
_tERMPAINT_PUBLIC _Bool termpaint_attr_set_patch_mustcheck(termpaint_attr* attr, _Bool optimize, const char *setup, const char * cleanup);
#define TERMPAINT_DEFAULT_COLOR 0x0000000
#define TERMPAINT_NAMED_COLOR 0x2100000

#define TERMPAINT_COLOR_BLACK TERMPAINT_NAMED_COLOR + 0
#define TERMPAINT_COLOR_RED TERMPAINT_NAMED_COLOR + 1
#define TERMPAINT_COLOR_GREEN TERMPAINT_NAMED_COLOR + 2
#define TERMPAINT_COLOR_YELLOW TERMPAINT_NAMED_COLOR + 3
#define TERMPAINT_COLOR_BLUE TERMPAINT_NAMED_COLOR + 4
#define TERMPAINT_COLOR_MAGENTA TERMPAINT_NAMED_COLOR + 5
#define TERMPAINT_COLOR_CYAN TERMPAINT_NAMED_COLOR + 6
#define TERMPAINT_COLOR_LIGHT_GREY TERMPAINT_NAMED_COLOR + 7

#define TERMPAINT_COLOR_DARK_GREY TERMPAINT_NAMED_COLOR + 8
#define TERMPAINT_COLOR_BRIGHT_RED TERMPAINT_NAMED_COLOR + 9
#define TERMPAINT_COLOR_BRIGHT_GREEN TERMPAINT_NAMED_COLOR + 10
#define TERMPAINT_COLOR_BRIGHT_YELLOW TERMPAINT_NAMED_COLOR + 11
#define TERMPAINT_COLOR_BRIGHT_BLUE TERMPAINT_NAMED_COLOR + 12
#define TERMPAINT_COLOR_BRIGHT_MAGENTA TERMPAINT_NAMED_COLOR + 13
#define TERMPAINT_COLOR_BRIGHT_CYAN TERMPAINT_NAMED_COLOR + 14
#define TERMPAINT_COLOR_WHITE TERMPAINT_NAMED_COLOR + 15

#define TERMPAINT_INDEXED_COLOR 0x2200000
#define TERMPAINT_RGB_COLOR_OFFSET 0x1000000
#define TERMPAINT_RGB_COLOR(r, g, b) (TERMPAINT_RGB_COLOR_OFFSET | (TERMPAINTP_CAST(unsigned,r) << 16) | (TERMPAINTP_CAST(unsigned, g) << 8) | TERMPAINTP_CAST(unsigned, b))

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

#define TERMPAINT_ERASED "\x7f"

_tERMPAINT_PUBLIC termpaint_surface *termpaint_terminal_new_surface(termpaint_terminal *term, int width, int height);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_terminal_new_surface_or_nullptr(termpaint_terminal *term, int width, int height);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_surface_new_surface(termpaint_surface *surface, int width, int height);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_surface_new_surface_or_nullptr(termpaint_surface *surface, int width, int height);
_tERMPAINT_PUBLIC termpaint_surface *termpaint_surface_duplicate(termpaint_surface *surface);
_tERMPAINT_PUBLIC void termpaint_surface_free(termpaint_surface *surface);
_tERMPAINT_PUBLIC void termpaint_surface_resize(termpaint_surface *surface, int width, int height);
_tERMPAINT_PUBLIC _Bool termpaint_surface_resize_mustcheck(termpaint_surface *surface, int width, int height);
_tERMPAINT_PUBLIC int termpaint_surface_width(const termpaint_surface *surface);
_tERMPAINT_PUBLIC int termpaint_surface_height(const termpaint_surface *surface);
_tERMPAINT_PUBLIC int termpaint_surface_char_width(const termpaint_surface *surface, int codepoint);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_len_colors(termpaint_surface *surface, int x, int y, const char *string, int len, int fg, int bg);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_len_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int len, int fg, int bg, int clip_x0, int clip_x1);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_attr(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_len_attr(termpaint_surface *surface, int x, int y, const char *string, int len, const termpaint_attr *attr);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_attr_clipped(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr, int clip_x0, int clip_x1);
_tERMPAINT_PUBLIC void termpaint_surface_write_with_len_attr_clipped(termpaint_surface *surface, int x, int y, const char *string, int len, const termpaint_attr *attr, int clip_x0, int clip_x1);
_tERMPAINT_PUBLIC void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg);
_tERMPAINT_PUBLIC void termpaint_surface_clear_with_char(termpaint_surface *surface, int fg, int bg, int codepoint);
_tERMPAINT_PUBLIC void termpaint_surface_clear_with_attr(termpaint_surface *surface, const termpaint_attr *attr);
_tERMPAINT_PUBLIC void termpaint_surface_clear_with_attr_char(termpaint_surface *surface, const termpaint_attr *attr, int codepoint);
_tERMPAINT_PUBLIC void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg);
_tERMPAINT_PUBLIC void termpaint_surface_clear_rect_with_char(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg, int codepoint);
_tERMPAINT_PUBLIC void termpaint_surface_clear_rect_with_attr(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr);
_tERMPAINT_PUBLIC void termpaint_surface_clear_rect_with_attr_char(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr, int codepoint);

_tERMPAINT_PUBLIC void termpaint_surface_set_fg_color(const termpaint_surface *surface, int x, int y, unsigned fg);
_tERMPAINT_PUBLIC void termpaint_surface_set_bg_color(const termpaint_surface *surface, int x, int y, unsigned bg);
_tERMPAINT_PUBLIC void termpaint_surface_set_deco_color(const termpaint_surface *surface, int x, int y, unsigned deco_color);

_tERMPAINT_PUBLIC void termpaint_surface_set_softwrap_marker(termpaint_surface *surface, int x, int y, _Bool state);
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
_tERMPAINT_PUBLIC _Bool termpaint_surface_peek_softwrap_marker(const termpaint_surface *surface, int x, int y);
_tERMPAINT_PUBLIC _Bool termpaint_surface_same_contents(const termpaint_surface *surface1, const termpaint_surface *surface2);

_tERMPAINT_PUBLIC termpaint_text_measurement* termpaint_text_measurement_new(const termpaint_surface *surface);
_tERMPAINT_PUBLIC termpaint_text_measurement* termpaint_text_measurement_new_or_nullptr(const termpaint_surface *surface);
_tERMPAINT_PUBLIC void termpaint_text_measurement_free(termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_reset(termpaint_text_measurement *m);

_tERMPAINT_PUBLIC int termpaint_text_measurement_pending_ref(const termpaint_text_measurement *m);

_tERMPAINT_PUBLIC int termpaint_text_measurement_last_codepoints(const termpaint_text_measurement *m);
_tERMPAINT_PUBLIC int termpaint_text_measurement_last_clusters(const termpaint_text_measurement *m);
_tERMPAINT_PUBLIC int termpaint_text_measurement_last_width(const termpaint_text_measurement *m);
_tERMPAINT_PUBLIC int termpaint_text_measurement_last_ref(const termpaint_text_measurement *m);

_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_codepoints(const termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_codepoints(termpaint_text_measurement *m, int new_value);
_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_clusters(const termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_clusters(termpaint_text_measurement *m, int new_value);
_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_width(const termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_width(termpaint_text_measurement *m, int new_value);
_tERMPAINT_PUBLIC int termpaint_text_measurement_limit_ref(const termpaint_text_measurement *m);
_tERMPAINT_PUBLIC void termpaint_text_measurement_set_limit_ref(termpaint_text_measurement *m, int new_value);

#define TERMPAINT_MEASURE_LIMIT_REACHED 1
#define TERMPAINT_MEASURE_NEW_CLUSTER 2

_tERMPAINT_PUBLIC int termpaint_text_measurement_feed_codepoint(termpaint_text_measurement *m, int ch, int ref_adjust);
_tERMPAINT_PUBLIC _Bool /* reached limit */ termpaint_text_measurement_feed_utf32(termpaint_text_measurement *m, const uint32_t *chars, int length, _Bool final);
_tERMPAINT_PUBLIC _Bool /* reached limit */ termpaint_text_measurement_feed_utf16(termpaint_text_measurement *m, const uint16_t *code_units, int length, _Bool final);
_tERMPAINT_PUBLIC _Bool /* reached limit */ termpaint_text_measurement_feed_utf8(termpaint_text_measurement *m, const char *code_units, int length, _Bool final);

#ifdef __cplusplus
}
#endif

#endif
