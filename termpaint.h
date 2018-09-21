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
#endif

struct termpaint_attr_;
typedef struct termpaint_attr_ termpaint_attr;

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

termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration);
void termpaint_terminal_free(termpaint_terminal *term);
void termpaint_terminal_free_with_restore(termpaint_terminal *term);
termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term);
void termpaint_terminal_flush(termpaint_terminal *term, _Bool full_repaint);
const char *termpaint_terminal_restore_sequence(termpaint_terminal *term);
void termpaint_terminal_reset_attributes(termpaint_terminal *term);
void termpaint_terminal_set_cursor(termpaint_terminal *term, int x, int y);

void termpaint_terminal_callback(termpaint_terminal *term);
void termpaint_terminal_set_raw_input_filter_cb(termpaint_terminal *term, _Bool (*cb)(void *user_data, const char *data, unsigned length, _Bool overflow), void *user_data);
void termpaint_terminal_set_event_cb(termpaint_terminal *term, void (*cb)(void *user_data, termpaint_event* event), void *user_data);
void termpaint_terminal_add_input_data(termpaint_terminal *term, const char *data, unsigned length);
const char* termpaint_terminal_peek_input_buffer(termpaint_terminal *term);
int termpaint_terminal_peek_input_buffer_length(termpaint_terminal *term);

_Bool termpaint_terminal_auto_detect(termpaint_terminal *terminal);
enum termpaint_auto_detect_state_enum { termpaint_auto_detect_none,
                                                 termpaint_auto_detect_running,
                                                 termpaint_auto_detect_done};
enum termpaint_auto_detect_state_enum termpaint_terminal_auto_detect_state(termpaint_terminal *terminal);
void termpaint_terminal_auto_detect_result_text(termpaint_terminal *terminal, char *buffer, int buffer_length);
void termpaint_terminal_setup_fullscreen(termpaint_terminal *terminal, int width, int height, const char *options);

termpaint_attr* termpaint_attr_new(int fg, int bg);
termpaint_attr* termpaint_attr_clone(termpaint_attr* attr);
void termpaint_attr_free(termpaint_attr* attr);
void termpaint_attr_set_fg(termpaint_attr* attr, int fg);
void termpaint_attr_set_bg(termpaint_attr* attr, int bg);
void termpaint_attr_set_deco(termpaint_attr* attr, int deco_color);
#define TERMPAINT_DEFAULT_COLOR 0x1000000
#define TERMPAINT_NAMED_COLOR 0x1100000
#define TERMPAINT_INDEXED_COLOR 0x1200000
#define TERMPAINT_RGB_COLOR(r, g, b) (((r) << 16) | ((b) << 8) | (g))

#define TERMPAINT_STYLE_BOLD (1<<0)
#define TERMPAINT_STYLE_ITALIC (1<<1)
#define TERMPAINT_STYLE_BLINK (1<<4)
#define TERMPAINT_STYLE_OVERLINE (1<<5)
#define TERMPAINT_STYLE_INVERSE (1<<6)
#define TERMPAINT_STYLE_STRIKE (1<<7)
#define TERMPAINT_STYLE_UNDERLINE (1<<16)
#define TERMPAINT_STYLE_UNDERLINE_DBL (1<<17)
#define TERMPAINT_STYLE_UNDERLINE_CURLY (1<<18)
void termpaint_attr_set_style(termpaint_attr* attr, int bits);
void termpaint_attr_unset_style(termpaint_attr* attr, int bits);
void termpaint_attr_reset_style(termpaint_attr* attr);

//void termpaint_surface_free(termpaint_surface *surface);
void termpaint_surface_resize(termpaint_surface *surface, int width, int height);
int termpaint_surface_width(termpaint_surface *surface);
int termpaint_surface_height(termpaint_surface *surface);
int termpaint_surface_char_width(termpaint_surface *surface, int codepoint);
void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg);
void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1);
void termpaint_surface_write_with_attr(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr);
void termpaint_surface_write_with_attr_clipped(termpaint_surface *surface, int x, int y, const char *string, const termpaint_attr *attr, int clip_x0, int clip_x1);
void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg);
void termpaint_surface_clear_with_attr(termpaint_surface *surface, const termpaint_attr *attr);
void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg);
void termpaint_surface_clear_rect_with_attr(termpaint_surface *surface, int x, int y, int width, int height, const termpaint_attr *attr);

#ifdef __cplusplus
}
#endif

#endif
