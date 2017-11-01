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

typedef struct termpaint_callbacks_ {
    _Bool (*got_response)(termpaint_surface *surface, char *data, int length);
} termpaint_callbacks;


typedef struct termpaint_integration_ {
    void (*free)(struct termpaint_integration_ *integration);
    void (*write)(struct termpaint_integration_ *integration, char *data, int length);
    void (*flush)(struct termpaint_integration_ *integration);
    _Bool (*is_bad)(struct termpaint_integration_ *integration);
    void (*expect_response)(struct termpaint_integration_ *integration);
} termpaint_integration;

termpaint_surface *termpaint_surface_new(termpaint_integration *integration);
void termpaint_surface_resize(termpaint_surface *surface, int width, int height);
int termpaint_surface_width(termpaint_surface *surface);
int termpaint_surface_height(termpaint_surface *surface);
void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg);
void termpaint_surface_write_with_colors_clipped(termpaint_surface *surface, int x, int y, const char *string, int fg, int bg, int clip_x0, int clip_x1);
void termpaint_surface_clear(termpaint_surface *surface, int bg);
void termpaint_surface_clear_rect(termpaint_surface *surface, int x, int y, int width, int height, int bg);

// May only be called on terminal surfaces
void termpaint_surface_reset_attributes(termpaint_surface *surface);
void termpaint_surface_flush(termpaint_surface *surface);
void termpaint_surface_set_cursor(termpaint_surface *surface, int x, int y);
_Bool termpaint_auto_detect(termpaint_surface *surface);

#ifdef __cplusplus
}
#endif

#endif
