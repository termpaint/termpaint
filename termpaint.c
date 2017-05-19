#include "termpaint.h"

#include <malloc.h>
#include <string.h>
#include <stdbool.h>

#include "termpaint_compiler.h"
#include "termpaint_utf8.h"

/* TODO and known problems:
 * What about composing code points?
 *
 *
 */

#define nullptr ((void*)0)

#define TERMPTR(var) ((termpaint_surface_terminal*)var)


typedef struct cell_ {
    unsigned char text[7]; // full 31bit range plus termination
    //_Bool double_width;
    int fg_color;
    int bg_color;
} cell;

struct termpaint_surface_ {
    cell* cells;
    int cells_allocated;
    int width;
    int height;
};

typedef struct termpaint_surface_terminal_ {
    termpaint_surface base;
    termpaint_integration *integration;
} termpaint_surface_terminal;


static void termpaintp_collapse(termpaint_surface *surface) {
    surface->width = 0;
    surface->height = 0;
    surface->cells_allocated = 0;
    surface->cells = nullptr;
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
        termpaintp_collapse(surface);
        return;
    }
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

termpaint_surface *termpaint_surface_new(termpaint_integration *integration) {
    termpaint_surface_terminal *ret = calloc(1, sizeof(termpaint_surface_terminal));

    // start collapsed
    termpaintp_collapse(&ret->base);
    ret->integration = integration;

    return (termpaint_surface*)ret;
}

static void int_puts(termpaint_integration *integration, char *str) {
    integration->write(integration, str, strlen(str));
}

static void int_put_num(termpaint_integration *integration, int num) {
    char buf[12];
    int len = sprintf(buf, "%d", num);
    integration->write(integration, buf, len);
}

void termpaint_surface_flush(termpaint_surface *surface) {
    termpaint_integration *integration = TERMPTR(surface)->integration;
    int_puts(integration, "\e[H");
    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < surface->width; x++) {
            cell* c = termpaintp_getcell(surface, x, y);
            if (*c->text == 0) {
                c->text[0] = ' ';
                c->text[1] = 0;
            }
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
                int_puts(integration, "39");
            }
            int_puts(integration, "m");
            int_puts(integration, (char*)c->text);
        }
        int_puts(integration, "\n");
    }
}

void termpaint_surface_write_with_colors(termpaint_surface *surface, int x, int y, const char *string_s, int fg, int bg) {
    const unsigned char *string = (const unsigned char *)string_s;
    while (*string) {
        if (x >= surface->width || y >= surface->height) {
            return;
        }

        cell *c = termpaintp_getcell(surface, x, y);
        c->fg_color = fg;
        c->bg_color = bg;

        int size = termpaintp_utf8_len(string[0]);

        for (int i = 0; i < size; i++) {
            if (string[i] == 0) {
                // bogus, bail
                return;
            }
            c->text[i] = string[i];
        }
        c->text[size] = 0;
        string += size;

        ++x;
    }

}


void termpaint_surface_clear(termpaint_surface *surface, int bg) {
    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < surface->width; x++) {
            cell* c = termpaintp_getcell(surface, x, y);
            c->text[0] = ' ';
            c->text[1] = 0;
            c->bg_color = bg;
            c->fg_color = 0;
        }
    }
}


void termpaint_surface_resize(termpaint_surface *surface, int width, int height) {
    if (width < 0 || height < 0) {
        free(surface->cells);
        termpaintp_collapse(surface);
    } else {
        termpaintp_resize(surface, width, height);
    }
}


bool termpaint_auto_detect(termpaint_surface *surface) {
    UNUSED(surface); // TODO
    return false;
}


void termpaint_surface_reset_attributes(termpaint_surface *surface) {
    termpaint_integration *integration = TERMPTR(surface)->integration;
    int_puts(integration, "\e[0m");
}
