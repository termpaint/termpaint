#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "termpaint.h"
#include "termpaintx.h"
#include "termpaintx_ttyrescue.h"

termpaint_terminal *terminal;
termpaint_surface *surface;
termpaint_integration *integration;

bool quit;

typedef struct event_ {
    int type;
    int modifier;
    const char *string;
    struct event_* next;
} event;

event* event_current;

void event_callback(void *userdata, termpaint_event *tp_event) {
    // remember tp_event is only valid while this callback runs, so copy everything we need.
    event *my_event = NULL;
    if (tp_event->type == TERMPAINT_EV_CHAR) {
        my_event = malloc(sizeof(event));
        my_event->type = tp_event->type;
        my_event->modifier = tp_event->c.modifier;
        my_event->string = strndup(tp_event->c.string, tp_event->c.length);
        my_event->next = NULL;
    } else if (tp_event->type == TERMPAINT_EV_KEY) {
        my_event = malloc(sizeof(event));
        my_event->type = tp_event->type;
        my_event->modifier = tp_event->key.modifier;
        my_event->string = strdup(tp_event->key.atom);
        my_event->next = NULL;
    }

    if (my_event) {
        event* prev = event_current;
        while (prev->next) {
            prev = prev->next;
        }
        prev->next = my_event;
    }
}

bool init(void) {
    event_current = malloc(sizeof(event));
    event_current->next = NULL;
    event_current->string = NULL;

    integration = termpaintx_full_integration_from_fd(1, 0, "+kbdsigint +kbdsigtstp");
    if (!integration) {
        puts("Could not init!");
        return 0;
    }

    terminal = termpaint_terminal_new(integration);
    termpaint_terminal_set_event_cb(terminal, event_callback, NULL);
    termpaintx_full_integration_set_terminal(integration, terminal);
    surface = termpaint_terminal_get_surface(terminal);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                           "Terminal auto detection is taking unusually long, press space to abort.");
    termpaintx_full_integration_apply_input_quirks(integration);
    int width, height;
    termpaintx_full_integration_terminal_size(integration, &width, &height);
    termpaint_terminal_setup_fullscreen(terminal, width, height, "+kbdsig");
    termpaintx_full_integration_ttyrescue_start(integration);

    return 1;
}

void cleanup(void) {
    termpaint_terminal_free_with_restore(terminal);

    while (event_current) {
        free((void*)event_current->string);
        event* next = event_current->next;
        free(event_current);
        event_current = next;
    }
}

event* key_wait(void) {
    termpaint_terminal_flush(terminal, false);

    while (!event_current->next) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            cleanup();
            exit(1);
        }
    }

    free((void*)event_current->string);
    event* next = event_current->next;
    free(event_current);
    event_current = next;
    return next;
}

void write_sample(termpaint_attr* attr_ui, termpaint_attr* attr_sample, int line, char const* name, int style) {
    termpaint_surface_write_with_attr(surface, 0, line, name, attr_ui);

    termpaint_attr_reset_style(attr_sample);
    termpaint_attr_set_style(attr_sample, style);
    termpaint_surface_write_with_attr(surface, 11, line, "Sample", attr_sample);
}

void repaint_samples(termpaint_attr* attr_ui, termpaint_attr* attr_sample)
{
    write_sample(attr_ui, attr_sample, 3, "No Style:", 0);
    write_sample(attr_ui, attr_sample, 4, "Bold:", TERMPAINT_STYLE_BOLD);
    write_sample(attr_ui, attr_sample, 5, "Italic:", TERMPAINT_STYLE_ITALIC);
    write_sample(attr_ui, attr_sample, 6, "Blinking:", TERMPAINT_STYLE_BLINK);
    write_sample(attr_ui, attr_sample, 7, "Underline:", TERMPAINT_STYLE_UNDERLINE);
    write_sample(attr_ui, attr_sample, 8, "Strikeout:", TERMPAINT_STYLE_STRIKE);
    write_sample(attr_ui, attr_sample, 9, "Inverse:", TERMPAINT_STYLE_INVERSE);

    write_sample(attr_ui, attr_sample, 11, "Overline:", TERMPAINT_STYLE_OVERLINE);
    write_sample(attr_ui, attr_sample, 12, "Dbl under:", TERMPAINT_STYLE_UNDERLINE_DBL);
    write_sample(attr_ui, attr_sample, 13, "curly:", TERMPAINT_STYLE_UNDERLINE_CURLY);

    // There is not yet explicit support for URLs, so use the low level patch interface
    termpaint_attr* attr_url = termpaint_attr_clone(attr_sample);
    termpaint_attr_set_patch(attr_url, true, "\e]8;;http://example.com\a", "\e]8;;\a");
    write_sample(attr_ui, attr_url, 14, "url:", 0);
    termpaint_attr_free(attr_url);
}

void repaint_all(termpaint_attr* attr_ui, termpaint_attr* attr_sample)
{
    termpaint_surface_clear_with_attr(surface, attr_ui);

    termpaint_surface_write_with_attr(surface, 1, 0, "Attribute Demo", attr_ui);

    repaint_samples(attr_ui, attr_sample);

    termpaint_surface_write_with_attr(surface, 25, 2, "Select Color", attr_ui);

    termpaint_surface_write_with_attr(surface, 2, 16, "q: Quit", attr_ui);
}

void update_current_key_display(termpaint_attr* attr_ui, event *evt) {
    if (evt->type == TERMPAINT_EV_CHAR || evt->type == TERMPAINT_EV_KEY) {
        char buff[100];
        snprintf(buff, 100, "%-20.20s mod: %d", evt->string, evt->modifier);
        termpaint_surface_write_with_attr(surface, 0, 23, "Last key: ", attr_ui);
        termpaint_surface_write_with_attr(surface, 11, 23, buff, attr_ui);
    }
}

void named_color_menu(termpaint_attr* attr_ui, termpaint_attr* attr_to_change, int which_color) {
    int color = 0;

    while (!quit) {
        {
            termpaint_attr* preview = termpaint_attr_new(0, TERMPAINT_INDEXED_COLOR + color);
            termpaint_surface_write_with_attr(surface, 50, 7, "  ", preview);
            termpaint_attr_free(preview);
        }
        termpaint_surface_write_with_attr(surface, 25, 7, "  Black", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 8, "  Red", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 9, "  Green", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 10, "  Yellow", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 11, "  Blue", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 12, "  Magenta", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 13, "  Cyan", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 14, "  Light Grey", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 15, "  Dark Grey", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 16, "  Bright Red", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 17, "  Bright Green", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 18, "  Bright Yellow", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 19, "  Bright Blue", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 20, "  Bright Magenta", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 21, "  Bright Cyan", attr_ui);
        termpaint_surface_write_with_attr(surface, 25, 22, "  White", attr_ui);

        termpaint_surface_write_with_attr(surface, 25, 7 + color, "*", attr_ui);

        event *evt = key_wait();
        update_current_key_display(attr_ui, evt);

        if (evt->type == TERMPAINT_EV_CHAR && strcmp(evt->string, "q") == 0) {
            quit = true;
        }

        if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowUp") == 0) {
            color = color - 1;
            if (color < 0) {
                color = 15;
            }
        }

        if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowDown") == 0) {
            color = (color + 1) % 16;
        }

        if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "Enter") == 0) {
            int tp_color = TERMPAINT_NAMED_COLOR + color;
            if (which_color == 0) {
                termpaint_attr_set_fg(attr_to_change, tp_color);
            } else if (which_color == 1) {
                termpaint_attr_set_bg(attr_to_change, tp_color);
            } else {
                termpaint_attr_set_deco(attr_to_change, tp_color);
            }
            return;
        }

        if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "Escape") == 0) {
            return;
        }
    }
}

void indexed_color_menu(termpaint_attr* attr_ui, termpaint_attr* attr_to_change, int which_color) {
    int color = 0;

    termpaint_surface_write_with_attr(surface, 25, 7, "  0", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 8, " 16", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 9, " 32", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 10, " 48", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 11, " 64", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 12, " 80", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 13, " 96", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 14, "112", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 15, "128", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 16, "144", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 17, "160", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 18, "176", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 19, "192", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 20, "208", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 21, "224", attr_ui);
    termpaint_surface_write_with_attr(surface, 25, 22, "240", attr_ui);

    termpaint_surface_write_with_attr(surface, 29, 6, "  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15", attr_ui);

    while (!quit) {
        {
            termpaint_attr* preview = termpaint_attr_new(0, TERMPAINT_INDEXED_COLOR + color);
            termpaint_surface_write_with_attr(surface, 28, 6, "  ", preview);
            termpaint_attr_free(preview);
        }
        termpaint_surface_clear_rect_with_attr(surface, 29, 7, 50, 16, attr_ui);
        char buff[11];
        sprintf(buff, "%3d", color);
        termpaint_surface_write_with_attr(surface, 29 + (color % 16) * 3, 7 + (color / 16), buff, attr_ui);

        event *evt = key_wait();
        update_current_key_display(attr_ui, evt);

        if (evt->type == TERMPAINT_EV_CHAR) {
            if (strcmp(evt->string, "q") == 0) {
                quit = true;
            }
        } else if (evt->type == TERMPAINT_EV_KEY) {
            if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowLeft") == 0) {
                color -= 1;
                if (color < 0) {
                    color = 255;
                }
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowRight") == 0) {
                color = (color + 1) % 256;
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowUp") == 0) {
                color -= 16;
                if (color < 0) {
                    color += 256;
                }
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowDown") == 0) {
                color = (color + 16) % 256;
            }

            if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "Enter") == 0) {
                int tp_color = TERMPAINT_INDEXED_COLOR + color;
                if (which_color == 0) {
                    termpaint_attr_set_fg(attr_to_change, tp_color);
                } else if (which_color == 1) {
                    termpaint_attr_set_bg(attr_to_change, tp_color);
                } else {
                    termpaint_attr_set_deco(attr_to_change, tp_color);
                }
                return;
            }

            if (strcmp(evt->string, "Escape") == 0) {
                return;
            }
        }
    }
}


void rgb_color_menu(termpaint_attr* attr_ui, termpaint_attr* attr_to_change, int which_color) {
    int red = 0;
    int green = 0;
    int blue = 0;

    int *selected = &red;

    termpaint_surface_write_with_attr(surface, 29, 10, "left/right: select component", attr_ui);
    termpaint_surface_write_with_attr(surface, 29, 11, "up/down: adjust value", attr_ui);
    termpaint_surface_write_with_attr(surface, 29, 12, "page up/page down: adjust value (16 increments)", attr_ui);
    termpaint_surface_write_with_attr(surface, 29, 13, "esc: abort", attr_ui);
    termpaint_surface_write_with_attr(surface, 29, 14, "enter: activate color", attr_ui);


    while (!quit) {
        char buff[40];
        sprintf(buff, "R: %3d G: %3d B: %3d", red, green, blue);
        termpaint_surface_write_with_attr(surface, 29, 7, buff, attr_ui);
        termpaint_surface_write_with_attr(surface, 29, 8, "                    ", attr_ui);

        {
            termpaint_attr* preview = termpaint_attr_new(0, TERMPAINT_RGB_COLOR(red, green, blue));
            termpaint_surface_write_with_attr(surface, 52, 7, "  ", preview);
            termpaint_attr_free(preview);
        }

        if (selected == &red) {
            termpaint_surface_write_with_attr(surface, 32, 8, "^^^", attr_ui);
        } else if (selected == &green) {
            termpaint_surface_write_with_attr(surface, 39, 8, "^^^", attr_ui);
        } else if (selected == &blue) {
            termpaint_surface_write_with_attr(surface, 46, 8, "^^^", attr_ui);
        }

        event *evt = key_wait();
        update_current_key_display(attr_ui, evt);

        if (evt->type == TERMPAINT_EV_CHAR) {
            if (strcmp(evt->string, "q") == 0) {
                quit = true;
            }
        } else if (evt->type == TERMPAINT_EV_KEY) {
            if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowLeft") == 0) {
                if (selected == &green) {
                    selected = &red;
                } else if (selected == &blue) {
                    selected = &green;
                }
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowRight") == 0) {
                if (selected == &red) {
                    selected = &green;
                } else if (selected == &green) {
                    selected = &blue;
                }
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowUp") == 0) {
                *selected = (256 + *selected - 1) % 256;
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowDown") == 0) {
                *selected = (*selected + 1) % 256;
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "PageUp") == 0) {
                *selected = (256 + *selected - 16) % 256;
            } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "PageDown") == 0) {
                *selected = (*selected + 16) % 256;
            }

            if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "Enter") == 0) {
                int tp_color = TERMPAINT_RGB_COLOR(red, green, blue);
                if (which_color == 0) {
                    termpaint_attr_set_fg(attr_to_change, tp_color);
                } else if (which_color == 1) {
                    termpaint_attr_set_bg(attr_to_change, tp_color);
                } else {
                    termpaint_attr_set_deco(attr_to_change, tp_color);
                }
                return;
            }

            if (strcmp(evt->string, "Escape") == 0) {
                return;
            }
        }

    }
}

void menu(termpaint_attr* attr_ui, termpaint_attr* attr_sample) {
    bool sample = true;

    bool reset = true;
    while (!quit) {
        if (reset) {
            repaint_all(attr_ui, attr_sample);

            termpaint_surface_write_with_attr(surface, 29, 14, "left/right: change select", attr_ui);
            termpaint_surface_write_with_attr(surface, 29, 15, "up/esc: undo choice", attr_ui);
            termpaint_surface_write_with_attr(surface, 29, 16, "enter: follow menu path", attr_ui);

            reset = false;
        }

        if (sample) {
            termpaint_surface_write_with_attr(surface, 25, 3, "* Sample", attr_ui);
            termpaint_surface_write_with_attr(surface, 40, 3, "  UI", attr_ui);
        } else {
            termpaint_surface_write_with_attr(surface, 25, 3, "  Sample", attr_ui);
            termpaint_surface_write_with_attr(surface, 40, 3, "* UI", attr_ui);
        }

        event *evt = key_wait();
        update_current_key_display(attr_ui, evt);

        if (evt->type == TERMPAINT_EV_CHAR && strcmp(evt->string, "q") == 0) {
            quit = true;
        }

        if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowLeft") == 0 && !sample) {
            sample = true;
        }

        if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowRight") == 0 && sample) {
            sample = false;
        }

        if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "Enter") == 0) {

            int which_color = 0;

            termpaint_surface_write_with_attr(surface, 25, 4, "* Foreground", attr_ui);
            termpaint_surface_write_with_attr(surface, 40, 4, "  Background", attr_ui);
            termpaint_surface_write_with_attr(surface, 54, 4, "  Deco", attr_ui);

            while (!quit && !reset) {
                event *evt = key_wait();
                update_current_key_display(attr_ui, evt);

                if (evt->type == TERMPAINT_EV_CHAR && strcmp(evt->string, "q") == 0) {
                    quit = true;
                }

                if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowLeft") == 0 && which_color == 1) {
                    termpaint_surface_write_with_attr(surface, 25, 4, "* Foreground", attr_ui);
                    termpaint_surface_write_with_attr(surface, 40, 4, "  Background", attr_ui);
                    which_color = 0;
                } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowLeft") == 0 && which_color == 2) {
                    termpaint_surface_write_with_attr(surface, 40, 4, "* Background", attr_ui);
                    termpaint_surface_write_with_attr(surface, 54, 4, "  Deco", attr_ui);
                    which_color = 1;
                }

                if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowRight") == 0 && which_color == 0) {
                    termpaint_surface_write_with_attr(surface, 25, 4, "  Foreground", attr_ui);
                    termpaint_surface_write_with_attr(surface, 40, 4, "* Background", attr_ui);
                    which_color = 1;
                } else if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "ArrowRight") == 0 && which_color == 1) {
                    termpaint_surface_write_with_attr(surface, 40, 4, "  Background", attr_ui);
                    termpaint_surface_write_with_attr(surface, 54, 4, "* Deco", attr_ui);
                    which_color = 2;
                }

                if (evt->type == TERMPAINT_EV_KEY && (strcmp(evt->string, "ArrowUp") == 0 || strcmp(evt->string, "Escape") == 0)) {
                    termpaint_surface_clear_rect_with_attr(surface, 25, 4, 35, 1, attr_ui);
                    break;
                }

                if (evt->type == TERMPAINT_EV_KEY && strcmp(evt->string, "Enter") == 0) {
                    int type = 0;

                    termpaint_surface_write_with_attr(surface, 25, 5, "* Named", attr_ui);
                    termpaint_surface_write_with_attr(surface, 40, 5, "  Indexed", attr_ui);
                    termpaint_surface_write_with_attr(surface, 53, 5, "  RGB", attr_ui);
                    while (!quit && !reset) {
                        event *evt = key_wait();
                        update_current_key_display(attr_ui, evt);

                        if (evt->type == TERMPAINT_EV_CHAR) {
                            if (strcmp(evt->string, "q") == 0) {
                                quit = true;
                            }
                        } else {
                            if (strcmp(evt->string, "ArrowLeft") == 0 && type == 1) {
                                type = 0;
                                termpaint_surface_write_with_attr(surface, 25, 5, "* Named", attr_ui);
                                termpaint_surface_write_with_attr(surface, 40, 5, "  Indexed", attr_ui);
                            } else if (strcmp(evt->string, "ArrowLeft") == 0 && type == 2) {
                                type = 1;
                                termpaint_surface_write_with_attr(surface, 40, 5, "* Indexed", attr_ui);
                                termpaint_surface_write_with_attr(surface, 53, 5, "  RGB", attr_ui);
                            } else if (strcmp(evt->string, "ArrowRight") == 0 && type == 0) {
                                type = 1;
                                termpaint_surface_write_with_attr(surface, 25, 5, "  Named", attr_ui);
                                termpaint_surface_write_with_attr(surface, 40, 5, "* Indexed", attr_ui);
                            } else if (strcmp(evt->string, "ArrowRight") == 0 && type == 1) {
                                type = 2;
                                termpaint_surface_write_with_attr(surface, 40, 5, "  Indexed", attr_ui);
                                termpaint_surface_write_with_attr(surface, 53, 5, "* RGB", attr_ui);
                            } else if (strcmp(evt->string, "ArrowUp") == 0 || strcmp(evt->string, "Escape") == 0) {
                                termpaint_surface_clear_rect_with_attr(surface, 25, 5, 35, 1, attr_ui);
                                break;
                            } else if (strcmp(evt->string, "Enter") == 0) {
                                termpaint_surface_clear_rect_with_attr(surface, 29, 14, 25, 3, attr_ui);

                                termpaint_attr* to_change = sample ? attr_sample : attr_ui;
                                if (type == 0) {
                                    named_color_menu(attr_ui, to_change, which_color);
                                } else if (type == 1) {
                                    indexed_color_menu(attr_ui, to_change, which_color);
                                } else {
                                    rgb_color_menu(attr_ui, to_change, which_color);
                                }
                                reset = true;
                            }
                        }
                    }
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (!init()) {
        return 1;
    }

    termpaint_attr* attr_ui = termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_attr* attr_sample = termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    repaint_all(attr_ui, attr_sample);

    menu(attr_ui, attr_sample);

    termpaint_attr_free(attr_sample);
    attr_sample = NULL;
    termpaint_attr_free(attr_ui);
    attr_ui = NULL;

    cleanup();
    return 0;
}
