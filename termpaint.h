#ifndef TERMPAINT_TERMPAINT_INCLUDED
#define TERMPAINT_TERMPAINT_INCLUDED

#ifdef __cplusplus
#ifndef _Bool
#define _Bool bool
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct termpaint_surface_;
typedef struct termpaint_surface_ termpaint_surface;

struct termpaint_terminal_;
typedef struct termpaint_terminal_ termpaint_terminal;

typedef struct termpaint_integration_ {
    void (*free)(struct termpaint_integration_ *integration);
    void (*write)(struct termpaint_integration_ *integration, char *data, int length);
    void (*flush)(struct termpaint_integration_ *integration);
    _Bool (*is_bad)(struct termpaint_integration_ *integration);
    //void (*request_callback)(struct termpaint_integration_ *integration);
} termpaint_integration;

termpaint_terminal *termpaint_terminal_new(termpaint_integration *integration);
void termpaint_terminal_free(termpaint_terminal *term);
termpaint_surface *termpaint_terminal_get_surface(termpaint_terminal *term);
void termpaint_terminal_flush(termpaint_terminal *term, _Bool full_repaint);
//void termpaint_terminal_callback(termpaint_terminal *term);
void termpaint_terminal_reset_attributes(termpaint_terminal *term);
void termpaint_terminal_set_cursor(termpaint_terminal *term, int x, int y);


//void termpaint_surface_free(termpaint_surface *surface);
void termpaint_surface_resize(termpaint_surface *surface, int width, int height);
int termpaint_surface_width(termpaint_surface *surface);
int termpaint_surface_height(termpaint_surface *surface);
void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg);
void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1);
void termpaint_surface_clear(termpaint_surface *surface, int fg, int bg);
void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int fg, int bg);

#ifdef __cplusplus
}
#endif

#endif
