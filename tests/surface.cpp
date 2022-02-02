// SPDX-License-Identifier: BSL-1.0
#include <string.h>
#include <map>
#include <limits>

#ifndef BUNDLED_CATCH2
#include "catch2/catch.hpp"
#else
#include "../third-party/catch.hpp"
#endif

#include <termpaint.h>

namespace {

template<typename T>
using DEL = void(T*);
template<typename T, DEL<T> del> struct Deleter{
    void operator()(T* t) { del(t); }
};

template<typename T, DEL<T> del>
struct unique_cptr : public std::unique_ptr<T, Deleter<T, del>> {
    operator T*() {
        return this->get();
    }

    static unique_cptr take_ownership(T* owning_raw_ptr) {
        unique_cptr ret;
        ret.reset(owning_raw_ptr);
        return ret;
    }
};

using uattr_ptr = unique_cptr<termpaint_attr, termpaint_attr_free>;
using usurface_ptr = unique_cptr<termpaint_surface, termpaint_surface_free>;


struct Fixture {
    Fixture(int width, int height) {

        auto free = [] (termpaint_integration* ptr) {
            termpaint_integration_deinit(ptr);
        };
        auto write = [] (termpaint_integration* ptr, const char *data, int length) {
            (void)ptr; (void)data; (void)length;
        };
        auto flush = [] (termpaint_integration* ptr) {
            (void)ptr;
        };
        termpaint_integration_init(&integration, free, write, flush);
        terminal.reset(termpaint_terminal_new(&integration));
        surface = termpaint_terminal_get_surface(terminal);
        termpaint_surface_resize(surface, width, height);
        termpaint_terminal_set_event_cb(terminal, [](void *, termpaint_event *) {}, nullptr);
    }

    unique_cptr<termpaint_terminal, termpaint_terminal_free_with_restore> terminal;
    termpaint_surface *surface;
    termpaint_integration integration;
};


class Cell {
public:
    std::string data;
    uint32_t fg = TERMPAINT_DEFAULT_COLOR;
    uint32_t bg = TERMPAINT_DEFAULT_COLOR;
    uint32_t deco = TERMPAINT_DEFAULT_COLOR;
    int style = 0;
    int width = 1;

    std::string setup;
    std::string cleanup;
    bool optimize = false;

    bool softWrapMarker = false;

public:
    Cell withFg(uint32_t val) {
        auto r = *this;
        r.fg = val;
        return r;
    }

    Cell withBg(uint32_t val) {
        auto r = *this;
        r.bg = val;
        return r;
    }

    Cell withDeco(uint32_t val) {
        auto r = *this;
        r.deco = val;
        return r;
    }

    Cell withStyle(int val) {
        auto r = *this;
        r.style = val;
        return r;
    }

    Cell withPatch(bool o, std::string s, std::string c) {
        auto r = *this;
        r.setup = s;
        r.cleanup = c;
        r.optimize = o;
        return r;
    }

    Cell withSoftWrapMarker() {
        auto r = *this;
        r.softWrapMarker = true;
        return r;
    }
};


Cell singleWideChar(std::string ch) {
    Cell c;
    c.data = ch;
    return c;
}


Cell doubleWideChar(std::string ch) {
    Cell c;
    c.data = ch;
    c.width = 2;
    return c;
}


Cell readCell(termpaint_surface *surface, int x, int y) {
    Cell cell;
    int len, left, right;
    const char *text = termpaint_surface_peek_text(surface, x, y, &len, &left, &right);
    if (left != x) {
        std::terminate();
    }
    cell.data = std::string(text, len);

    cell.width = right - left + 1;

    cell.fg = termpaint_surface_peek_fg_color(surface, x, y);
    cell.bg = termpaint_surface_peek_bg_color(surface, x, y);
    cell.deco = termpaint_surface_peek_deco_color(surface, x, y);
    cell.style = termpaint_surface_peek_style(surface, x, y);
    cell.softWrapMarker = termpaint_surface_peek_softwrap_marker(surface, x, y);

    const char* setup;
    const char* cleanup;
    bool optimize;
    termpaint_surface_peek_patch(surface, x, y, &setup, &cleanup, &optimize);
    if (setup || cleanup) {
        cell.setup = setup;
        cell.cleanup = cleanup;
        cell.optimize = optimize;
    }
    return cell;
}

static void checkEmptyPlusSome(termpaint_surface *surface, const std::map<std::tuple<int,int>, Cell> &some,
                               Cell empty = singleWideChar(TERMPAINT_ERASED)) {
    const int width = termpaint_surface_width(surface);
    const int height = termpaint_surface_height(surface);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; /* see loop body */ ) {
            Cell cell = readCell(surface, x, y);
            CAPTURE(x);
            CAPTURE(y);
            const Cell *expected;;
            if (some.count({x, y})) {
                expected = &some.at({x, y});
            } else {
                expected = &empty;
            }
            CHECK(cell.bg == expected->bg);
            CHECK(cell.fg == expected->fg);
            CHECK(cell.deco == expected->deco);
            CHECK(cell.data == expected->data);
            CHECK(cell.style == expected->style);
            CHECK(cell.width == expected->width);
            CHECK(cell.setup == expected->setup);
            CHECK(cell.cleanup == expected->cleanup);
            CHECK(cell.optimize == expected->optimize);
            CHECK(cell.softWrapMarker == expected->softWrapMarker);
            x += cell.width;
        }
    }
}

}


TEST_CASE("simple") {
    Fixture f{80, 24};

    CHECK(termpaint_surface_width(f.surface) == 80);
    CHECK(termpaint_surface_height(f.surface) == 24);

    checkEmptyPlusSome(f.surface, {});
}


TEST_CASE("resize") {
    Fixture f{80, 24};

    CHECK(termpaint_surface_width(f.surface) == 80);
    CHECK(termpaint_surface_height(f.surface) == 24);

    checkEmptyPlusSome(f.surface, {});

    termpaint_surface_resize(f.surface, 120, 40);

    CHECK(termpaint_surface_width(f.surface) == 120);
    CHECK(termpaint_surface_height(f.surface) == 40);

    checkEmptyPlusSome(f.surface, {});
}


TEST_CASE("resize - oversized") {
    Fixture f{80, 24};
    termpaint_surface_resize(f.surface, std::numeric_limits<int>::max() / 2, std::numeric_limits<int>::max() / 2);

    CHECK(termpaint_surface_width(f.surface) == 0);
    CHECK(termpaint_surface_height(f.surface) == 0);
}


TEST_CASE("simple text") {
    Fixture f{80, 6};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 10, 3 }, singleWideChar("S")},
        {{ 11, 3 }, singleWideChar("a")},
        {{ 12, 3 }, singleWideChar("m")},
        {{ 13, 3 }, singleWideChar("p")},
        {{ 14, 3 }, singleWideChar("l")},
        {{ 15, 3 }, singleWideChar("e")},
    });
}


TEST_CASE("simple text - with len and color") {
    Fixture f{80, 6};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_write_with_len_colors(f.surface, 10, 3, "SampleX", 6,
                                            TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLACK);

    checkEmptyPlusSome(f.surface, {
        {{ 10, 3 }, singleWideChar("S").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 11, 3 }, singleWideChar("a").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 12, 3 }, singleWideChar("m").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 13, 3 }, singleWideChar("p").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 14, 3 }, singleWideChar("l").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 15, 3 }, singleWideChar("e").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
    });
}


TEST_CASE("simple text - with len, color and clipped") {
    Fixture f{80, 6};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_write_with_len_colors_clipped(f.surface, 10, 3, "SampleX", 6,
                                                    TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLACK, 12, 80);

    checkEmptyPlusSome(f.surface, {
        {{ 12, 3 }, singleWideChar("m").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 13, 3 }, singleWideChar("p").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 14, 3 }, singleWideChar("l").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 15, 3 }, singleWideChar("e").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK)},
    });
}


TEST_CASE("simple text - with len and attr") {
    Fixture f{80, 6};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_attr *attr = termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLACK);
    termpaint_attr_set_style(attr, TERMPAINT_STYLE_BOLD);

    termpaint_surface_write_with_len_attr_clipped(f.surface, 10, 3, "SampleX", 6,
                                                  attr, 0, 80);

    checkEmptyPlusSome(f.surface, {
        {{ 10, 3 }, singleWideChar("S").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 11, 3 }, singleWideChar("a").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 12, 3 }, singleWideChar("m").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 13, 3 }, singleWideChar("p").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 14, 3 }, singleWideChar("l").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 15, 3 }, singleWideChar("e").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
    });

    termpaint_attr_free(attr);
}


TEST_CASE("simple text - with len, attr and clipped") {
    Fixture f{80, 6};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_attr *attr = termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLACK);
    termpaint_attr_set_style(attr, TERMPAINT_STYLE_BOLD);

    termpaint_surface_write_with_len_attr_clipped(f.surface, 10, 3, "SampleX", 6,
                                                  attr, 12, 80);

    checkEmptyPlusSome(f.surface, {
        {{ 12, 3 }, singleWideChar("m").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 13, 3 }, singleWideChar("p").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 14, 3 }, singleWideChar("l").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 15, 3 }, singleWideChar("e").withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLACK).withStyle(TERMPAINT_STYLE_BOLD)},
    });

    termpaint_attr_free(attr);
}


TEST_CASE("double width") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, doubleWideChar("あ")},
        {{ 5, 3 }, doubleWideChar("え")},
    });

    int len, left, right;
    const char* data = termpaint_surface_peek_text(f.surface, 3, 3, &len, &left, &right);
    CHECK(left == 3);
    CHECK(right == 4);
    CHECK(len == strlen("あ"));
    CHECK(std::string(data, len) == std::string("あ"));

    data = termpaint_surface_peek_text(f.surface, 4, 3, &len, &left, &right);
    CHECK(left == 3);
    CHECK(right == 4);
    CHECK(len == strlen("あ"));
    CHECK(std::string(data, len) == std::string("あ"));
}


TEST_CASE("chars that get substituted") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 3, "a\004\u00ad\u0088x", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("a")},
        {{ 4, 3 }, singleWideChar(" ")},
        {{ 5, 3 }, singleWideChar("-")},
        {{ 6, 3 }, singleWideChar(" ")},
        {{ 7, 3 }, singleWideChar("x")},
    });
}


TEST_CASE("write clear char") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 3, "a\x7fx", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 4, "\x7fx", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 5, "\x7f\u0308", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("a")},
        {{ 4, 3 }, singleWideChar(TERMPAINT_ERASED)},
        {{ 5, 3 }, singleWideChar("x")},
        {{ 3, 4 }, singleWideChar(TERMPAINT_ERASED)},
        {{ 4, 4 }, singleWideChar("x")},
        {{ 3, 5 }, singleWideChar(TERMPAINT_ERASED)},
        {{ 4, 5 }, singleWideChar("\u00a0\u0308")},
    });
}


TEST_CASE("vanish chars") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_COLOR_GREEN);

    termpaint_surface_write_with_colors(f.surface, 4, 3, "ab", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_GREEN).withFg(TERMPAINT_COLOR_RED)},
        {{ 4, 3 }, singleWideChar("a").withBg(TERMPAINT_COLOR_BLUE).withFg(TERMPAINT_COLOR_YELLOW)},
        {{ 5, 3 }, singleWideChar("b").withBg(TERMPAINT_COLOR_BLUE).withFg(TERMPAINT_COLOR_YELLOW)},
        {{ 6, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_GREEN).withFg(TERMPAINT_COLOR_RED)},
    });
}

TEST_CASE("vanish chars - misaligned wide") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_COLOR_GREEN);

    termpaint_surface_write_with_colors(f.surface, 4, 3, "ア", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_GREEN).withFg(TERMPAINT_COLOR_RED)},
        {{ 4, 3 }, doubleWideChar("ア").withBg(TERMPAINT_COLOR_BLUE).withFg(TERMPAINT_COLOR_YELLOW)},
        {{ 6, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_GREEN).withFg(TERMPAINT_COLOR_RED)},
    });
}

TEST_CASE("rgb colors") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 3, "r", TERMPAINT_RGB_COLOR(255, 128, 128), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 4, 3, "g", TERMPAINT_RGB_COLOR(128, 255, 128), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 5, 3, "b", TERMPAINT_RGB_COLOR(128, 128, 255), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 4, "r", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(255, 128, 128));
    termpaint_surface_write_with_colors(f.surface, 4, 4, "g", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(128, 255, 128));
    termpaint_surface_write_with_colors(f.surface, 5, 4, "b", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(128, 128, 255));

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("r").withFg(TERMPAINT_RGB_COLOR(0xff, 0x80, 0x80))},
        {{ 4, 3 }, singleWideChar("g").withFg(TERMPAINT_RGB_COLOR(0x80, 0xff, 0x80))},
        {{ 5, 3 }, singleWideChar("b").withFg(TERMPAINT_RGB_COLOR(0x80, 0x80, 0xff))},
        {{ 3, 4 }, singleWideChar("r").withBg(TERMPAINT_RGB_COLOR(0xff, 0x80, 0x80))},
        {{ 4, 4 }, singleWideChar("g").withBg(TERMPAINT_RGB_COLOR(0x80, 0xff, 0x80))},
        {{ 5, 4 }, singleWideChar("b").withBg(TERMPAINT_RGB_COLOR(0x80, 0x80, 0xff))},
    });
}


TEST_CASE("named fg colors") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3,  3, " ", TERMPAINT_COLOR_BLACK, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 4,  3, " ", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 5,  3, " ", TERMPAINT_COLOR_GREEN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 6,  3, " ", TERMPAINT_COLOR_YELLOW, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 7,  3, " ", TERMPAINT_COLOR_BLUE, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 8,  3, " ", TERMPAINT_COLOR_MAGENTA, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 9,  3, " ", TERMPAINT_COLOR_CYAN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 10, 3, " ", TERMPAINT_COLOR_LIGHT_GREY, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3,  4, " ", TERMPAINT_COLOR_DARK_GREY, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 4,  4, " ", TERMPAINT_COLOR_BRIGHT_RED, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 5,  4, " ", TERMPAINT_COLOR_BRIGHT_GREEN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 6,  4, " ", TERMPAINT_COLOR_BRIGHT_YELLOW, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 7,  4, " ", TERMPAINT_COLOR_BRIGHT_BLUE, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 8,  4, " ", TERMPAINT_COLOR_BRIGHT_MAGENTA, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 9,  4, " ", TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 10, 4, " ", TERMPAINT_COLOR_WHITE, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BLACK)},
        {{ 3, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_DARK_GREY)},
        {{ 4, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_RED)},
        {{ 4, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_RED)},
        {{ 5, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_GREEN)},
        {{ 5, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 6, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW)},
        {{ 6, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_YELLOW)},
        {{ 7, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BLUE)},
        {{ 7, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_BLUE)},
        {{ 8, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_MAGENTA)},
        {{ 8, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_MAGENTA)},
        {{ 9, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_CYAN)},
        {{ 9, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN)},
        {{ 10, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_LIGHT_GREY)},
        {{ 10, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_WHITE)},
    });
}


TEST_CASE("named bg colors") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(f.surface, 4,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_RED);
    termpaint_surface_write_with_colors(f.surface, 5,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 6,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_write_with_colors(f.surface, 7,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BLUE);
    termpaint_surface_write_with_colors(f.surface, 8,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_MAGENTA);
    termpaint_surface_write_with_colors(f.surface, 9,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_CYAN);
    termpaint_surface_write_with_colors(f.surface, 10, 3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_LIGHT_GREY);
    termpaint_surface_write_with_colors(f.surface, 3,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_DARK_GREY);
    termpaint_surface_write_with_colors(f.surface, 4,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_write_with_colors(f.surface, 5,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_GREEN);
    termpaint_surface_write_with_colors(f.surface, 6,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_YELLOW);
    termpaint_surface_write_with_colors(f.surface, 7,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_BLUE);
    termpaint_surface_write_with_colors(f.surface, 8,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_MAGENTA);
    termpaint_surface_write_with_colors(f.surface, 9,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_CYAN);
    termpaint_surface_write_with_colors(f.surface, 10, 4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_WHITE);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BLACK)},
        {{ 3, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_DARK_GREY)},
        {{ 4, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_RED)},
        {{ 4, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_RED)},
        {{ 5, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_GREEN)},
        {{ 5, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 6, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_YELLOW)},
        {{ 6, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_YELLOW)},
        {{ 7, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BLUE)},
        {{ 7, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_BLUE)},
        {{ 8, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 8, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_MAGENTA)},
        {{ 9, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_CYAN)},
        {{ 9, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_CYAN)},
        {{ 10, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_LIGHT_GREY)},
        {{ 10, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_WHITE)},
    });
}


TEST_CASE("indexed colors") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 16);
    termpaint_surface_write_with_colors(f.surface, 4,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 51);
    termpaint_surface_write_with_colors(f.surface, 5,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 70);
    termpaint_surface_write_with_colors(f.surface, 6,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 110);
    termpaint_surface_write_with_colors(f.surface, 7,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 123);
    termpaint_surface_write_with_colors(f.surface, 8,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 213);
    termpaint_surface_write_with_colors(f.surface, 9,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 232);
    termpaint_surface_write_with_colors(f.surface, 10, 3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 255);
    termpaint_surface_write_with_colors(f.surface, 3,  4, " ", TERMPAINT_INDEXED_COLOR +  16, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 4,  4, " ", TERMPAINT_INDEXED_COLOR +  51, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 5,  4, " ", TERMPAINT_INDEXED_COLOR +  70, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 6,  4, " ", TERMPAINT_INDEXED_COLOR + 110, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 7,  4, " ", TERMPAINT_INDEXED_COLOR + 123, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 8,  4, " ", TERMPAINT_INDEXED_COLOR + 213, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 9,  4, " ", TERMPAINT_INDEXED_COLOR + 232, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 10, 4, " ", TERMPAINT_INDEXED_COLOR + 255, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 16)},
        {{ 3, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 16)},
        {{ 4, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 51)},
        {{ 4, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 51)},
        {{ 5, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 70)},
        {{ 5, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 70)},
        {{ 6, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 110)},
        {{ 6, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 110)},
        {{ 7, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 123)},
        {{ 7, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 123)},
        {{ 8, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 213)},
        {{ 8, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 213)},
        {{ 9, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 232)},
        {{ 9, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 232)},
        {{ 10, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 255)},
        {{ 10, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 255)},
    });
}


TEST_CASE("attributes") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BOLD);
    termpaint_surface_write_with_attr(f.surface, 3, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_ITALIC);
    termpaint_surface_write_with_attr(f.surface, 4, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BLINK);
    termpaint_surface_write_with_attr(f.surface, 5, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_INVERSE);
    termpaint_surface_write_with_attr(f.surface, 6, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_STRIKE);
    termpaint_surface_write_with_attr(f.surface, 7, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE);
    termpaint_surface_write_with_attr(f.surface, 8, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_DBL);
    termpaint_surface_write_with_attr(f.surface, 9, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_CURLY);
    termpaint_surface_write_with_attr(f.surface, 10, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_OVERLINE);
    termpaint_surface_write_with_attr(f.surface, 11, 3, "X", attr.get());

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 4, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_ITALIC)},
        {{ 5, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_BLINK)},
        {{ 6, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_INVERSE)},
        {{ 7, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_STRIKE)},
        {{ 8, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE)},
        {{ 9, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_DBL)},
        {{ 10, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY)},
        {{ 11, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_OVERLINE)},
    });
}


TEST_CASE("simple patch") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_attr* attr_url = termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_attr_set_patch(attr_url, true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\");
    termpaint_surface_write_with_attr(f.surface, 3, 3, "ABC", attr_url);
    termpaint_attr_free(attr_url);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("A").withPatch(true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\")},
        {{ 4, 3 }, singleWideChar("B").withPatch(true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\")},
        {{ 5, 3 }, singleWideChar("C").withPatch(true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\")},
    });
}


TEST_CASE("too many patches") {
    // white-box: There is an internal limit of 255 different patch settings at one time.
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_attr* attr_url = termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    using namespace std::literals;
    std::map<std::tuple<int,int>, Cell> expected;
    for (int i = 0; i < 256; i++) {
        const std::string setup = "\033]8;;http://example.com\033\\"s + std::to_string(i);
        termpaint_attr_set_patch(attr_url, true, setup.data(), "\033]8;;\033\\");
        termpaint_surface_write_with_attr(f.surface, i % 80, i / 80, "x", attr_url);
        expected[{i % 80, i / 80}] = singleWideChar("x").withPatch(true, setup, "\033]8;;\033\\");
    }
    termpaint_attr_free(attr_url);

    expected[{255 % 80, 255 / 80}] = singleWideChar("x");

    checkEmptyPlusSome(f.surface, expected);
}


TEST_CASE("many patches - sequential") {
    // white-box: There is an internal limit of 255 different patch settings at one time.
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_attr* attr_url = termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    using namespace std::literals;
    for (int i = 0; i < 512; i++) {
        const std::string setup = "\033]8;;http://example.com\033\\"s + std::to_string(i);
        termpaint_attr_set_patch(attr_url, true, setup.data(), "\033]8;;\033\\");
        termpaint_surface_write_with_attr(f.surface, 0, 0, "x", attr_url);
        checkEmptyPlusSome(f.surface, {
            {{0, 0}, singleWideChar("x").withPatch(true, setup, "\033]8;;\033\\")}
        });
    }
    termpaint_attr_free(attr_url);
}


TEST_CASE("write with right clipping") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 75, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 75, 3 }, singleWideChar("S")},
        {{ 76, 3 }, singleWideChar("a")},
        {{ 77, 3 }, singleWideChar("m")},
        {{ 78, 3 }, singleWideChar("p")},
        {{ 79, 3 }, singleWideChar("l")},
    });
}


TEST_CASE("write with right clipping - double width complete") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 78, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 78, 3 }, doubleWideChar("あ")},
    });
}


TEST_CASE("write with right clipping - double width partial") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 77, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_RED);

    checkEmptyPlusSome(f.surface, {
        {{ 77, 3 }, doubleWideChar("あ").withBg(TERMPAINT_COLOR_RED)},
        {{ 79, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_RED)},
    });
}


TEST_CASE("write with overlong clip_x1") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, 75, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR, 0, 90);

    checkEmptyPlusSome(f.surface, {
        {{ 75, 3 }, singleWideChar("S")},
        {{ 76, 3 }, singleWideChar("a")},
        {{ 77, 3 }, singleWideChar("m")},
        {{ 78, 3 }, singleWideChar("p")},
        {{ 79, 3 }, singleWideChar("l")},
    });
}


TEST_CASE("write with negative y") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, 3, -1, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR, 0, 90);

    checkEmptyPlusSome(f.surface, {
    });
}


TEST_CASE("write with y below surface") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, 3, 80, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR, 0, 90);

    checkEmptyPlusSome(f.surface, {
    });
}


TEST_CASE("write with left clipping") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, -2, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR, 0, 90);

    checkEmptyPlusSome(f.surface, {
        {{ 0, 3 }, singleWideChar("m")},
        {{ 1, 3 }, singleWideChar("p")},
        {{ 2, 3 }, singleWideChar("l")},
        {{ 3, 3 }, singleWideChar("e")},
    });
}


TEST_CASE("write with left clipping - double width complete") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, -2, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR, 0, 90);

    checkEmptyPlusSome(f.surface, {
        {{ 0, 3 }, doubleWideChar("え")},
    });
}


TEST_CASE("write with left clipping - double width partial") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, -1, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_RED, 0, 90);

    checkEmptyPlusSome(f.surface, {
        {{ 0, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_RED)},
        {{ 1, 3 }, doubleWideChar("え").withBg(TERMPAINT_COLOR_RED)},
    });
}


TEST_CASE("write with negative clip_x0") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, -2, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR, -2, 79);

    checkEmptyPlusSome(f.surface, {
        {{ 0, 3 }, singleWideChar("m")},
        {{ 1, 3 }, singleWideChar("p")},
        {{ 2, 3 }, singleWideChar("l")},
        {{ 3, 3 }, singleWideChar("e")},
    });
}


TEST_CASE("write e plus non spaceing combining mark") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 5, 3, "\u0308e", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 5, 3 }, singleWideChar("\u00a0\u0308")},
        {{ 6, 3 }, singleWideChar("e")},
    });
}


TEST_CASE("write starting with non spaceing combining mark") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 5, 3, "e\u0308e", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 5, 3 }, singleWideChar("e\u0308")},
        {{ 6, 3 }, singleWideChar("e")},
    });
}


TEST_CASE("double width with right clipping") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, 3, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR, 0, 5);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, doubleWideChar("あ").withFg(TERMPAINT_COLOR_RED)},
        {{ 5, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_RED)},
    });
}


TEST_CASE("double width with left clipping") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors_clipped(f.surface, 3, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR, 4, 6);

    checkEmptyPlusSome(f.surface, {
        {{ 4, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_RED)},
        {{ 5, 3 }, doubleWideChar("え").withFg(TERMPAINT_COLOR_RED)},
    });
}


TEST_CASE("peek text") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 3, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR);

    int left, right;
    int len;
    const char *data;

    len = -2;
    data = termpaint_surface_peek_text(f.surface, 3, 3, &len, nullptr, nullptr);
    REQUIRE(len == strlen("あ"));
    CHECK(std::string(data, len) == std::string("あ"));

    left = -2;
    data = termpaint_surface_peek_text(f.surface, 3, 3, &len, &left, nullptr);
    CHECK(left == 3);

    right = -2;
    data = termpaint_surface_peek_text(f.surface, 3, 3, &len, nullptr, &right);
    CHECK(right == 4);
}

TEST_CASE("peek text on left most column") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(f.surface, 0, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR);

    int left, right;
    int len;
    const char *data;

    len = -2;
    data = termpaint_surface_peek_text(f.surface, 0, 3, &len, nullptr, nullptr);
    REQUIRE(len == strlen("あ"));
    CHECK(std::string(data, len) == std::string("あ"));

    left = -2;
    data = termpaint_surface_peek_text(f.surface, 0, 3, &len, &left, nullptr);
    CHECK(left == 0);

    right = -2;
    data = termpaint_surface_peek_text(f.surface, 0, 3, &len, nullptr, &right);
    CHECK(right == 1);
}


TEST_CASE("cluster with more than 8 bytes") {
    // white-box: Storage switches mode at 8 bytes length
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    std::string big_cluster = "e\u0308\u0308\u0308\u0308";
    termpaint_surface_write_with_colors(f.surface, 3, 3, big_cluster.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(big_cluster)},
    });
}


TEST_CASE("gc of cluster with more than 8 bytes (overwrite one char)") {
    // white-box: try to trigger garbage collecton for storage of more than 8 bytes long data.
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    // Add some other cells
    termpaint_surface_write_with_colors(f.surface, 5, 3, "abあ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    std::string big_cluster;
    for (char ch = 'a'; ch <= 'z'; ch++) {
        big_cluster = std::string(1, ch) + std::string("\u0308\u0308\u0308\u0308");
        termpaint_surface_write_with_colors(f.surface, 3, 3, big_cluster.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    }

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(big_cluster)},
        {{ 5, 3 }, singleWideChar("a")},
        {{ 6, 3 }, singleWideChar("b")},
        {{ 7, 3 }, doubleWideChar("あ")},
    });
}


TEST_CASE("gc of cluster with more than 8 bytes (with one to keep)") {
    // white-box: try to trigger garbage collecton for storage of more than 8 bytes long data.
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    // Add some other cells
    termpaint_surface_write_with_colors(f.surface, 5, 3, "abあ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_write_with_colors(f.surface, 5, 5, " \u0308\u0308\u0308\u0308", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);


    std::string big_cluster;
    for (char ch = 'a'; ch <= 'z'; ch++) {
        big_cluster = std::string(1, ch) + std::string("\u0308\u0308\u0308\u0308");
        termpaint_surface_write_with_colors(f.surface, 3, 3, big_cluster.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    }

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar(big_cluster)},
        {{ 5, 3 }, singleWideChar("a")},
        {{ 6, 3 }, singleWideChar("b")},
        {{ 7, 3 }, doubleWideChar("あ")},
        {{ 5, 5 }, singleWideChar(" \u0308\u0308\u0308\u0308")},
    });
}


// clear is implicitly tested all over the place


TEST_CASE("clear_with_char") {
    Fixture f{80, 24};

    struct TestCase { int ch; std::string s; };
    auto testCase = GENERATE(
                TestCase{' ', " "},
                TestCase{'a', "a"},
                TestCase{'\x7f', TERMPAINT_ERASED},
                TestCase{u'ä', "ä"},
                TestCase{u'あ', TERMPAINT_ERASED},
                TestCase{u'\u0308', TERMPAINT_ERASED});
    CAPTURE(testCase.ch);

    termpaint_surface_clear_with_char(f.surface, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE, testCase.ch);

    checkEmptyPlusSome(f.surface, {
        },
        singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE));
}


TEST_CASE("clear_with_attr") {
    Fixture f{80, 24};

    auto test_style = GENERATE(0, TERMPAINT_STYLE_BOLD, TERMPAINT_STYLE_ITALIC, TERMPAINT_STYLE_BLINK,
                               TERMPAINT_STYLE_INVERSE, TERMPAINT_STYLE_STRIKE, TERMPAINT_STYLE_UNDERLINE,
                               TERMPAINT_STYLE_UNDERLINE_DBL, TERMPAINT_STYLE_UNDERLINE_CURLY, TERMPAINT_STYLE_OVERLINE);
    CAPTURE(test_style);

    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE));
    termpaint_attr_set_style(attr.get(), test_style);

    termpaint_surface_clear_with_attr(f.surface, attr);

    checkEmptyPlusSome(f.surface, {
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                       .withStyle(test_style));
}


TEST_CASE("clear_with_attr_char") {
    Fixture f{80, 24};

    auto test_style = GENERATE(0, TERMPAINT_STYLE_BOLD, TERMPAINT_STYLE_ITALIC, TERMPAINT_STYLE_BLINK,
                               TERMPAINT_STYLE_INVERSE, TERMPAINT_STYLE_STRIKE, TERMPAINT_STYLE_UNDERLINE,
                               TERMPAINT_STYLE_UNDERLINE_DBL, TERMPAINT_STYLE_UNDERLINE_CURLY, TERMPAINT_STYLE_OVERLINE);
    CAPTURE(test_style);

    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE));
    termpaint_attr_set_style(attr.get(), test_style);

    struct TestCase { int ch; std::string s; };
    auto testCase = GENERATE(
                TestCase{' ', " "},
                TestCase{'a', "a"},
                TestCase{'\x7f', TERMPAINT_ERASED},
                TestCase{u'ä', "ä"},
                TestCase{u'あ', TERMPAINT_ERASED},
                TestCase{u'\u0308', TERMPAINT_ERASED});
    CAPTURE(testCase.ch);

    termpaint_surface_clear_with_attr_char(f.surface, attr, testCase.ch);

    checkEmptyPlusSome(f.surface, {
        },
        singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                       .withStyle(test_style));
}


TEST_CASE("clear_rect") {
    Fixture f{80, 24};
    termpaint_surface_clear_with_char(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN, '/');

    termpaint_surface_clear_rect(f.surface, 20, 12, 2, 3, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
            {{20, 12}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{21, 12}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{20, 13}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{21, 13}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{20, 14}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{21, 14}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
        },
        singleWideChar("/").withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("clear_rect_with_char") {
    Fixture f{80, 24};
    termpaint_surface_clear_with_char(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN, '/');

    struct TestCase { int ch; std::string s; };
    auto testCase = GENERATE(
                TestCase{' ', " "},
                TestCase{'a', "a"},
                TestCase{'\x7f', TERMPAINT_ERASED},
                TestCase{u'ä', "ä"},
                TestCase{u'あ', TERMPAINT_ERASED},
                TestCase{u'\u0308', TERMPAINT_ERASED});
    CAPTURE(testCase.ch);

    termpaint_surface_clear_rect_with_char(f.surface, 20, 12, 2, 3, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE, testCase.ch);

    checkEmptyPlusSome(f.surface, {
            {{20, 12}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{21, 12}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{20, 13}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{21, 13}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{20, 14}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
            {{21, 14}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
        },
        singleWideChar("/").withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("clear_rect_with_attr") {
    Fixture f{80, 24};
    termpaint_surface_clear_with_char(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN, '/');

    auto test_style = GENERATE(0, TERMPAINT_STYLE_BOLD, TERMPAINT_STYLE_ITALIC, TERMPAINT_STYLE_BLINK,
                               TERMPAINT_STYLE_INVERSE, TERMPAINT_STYLE_STRIKE, TERMPAINT_STYLE_UNDERLINE,
                               TERMPAINT_STYLE_UNDERLINE_DBL, TERMPAINT_STYLE_UNDERLINE_CURLY, TERMPAINT_STYLE_OVERLINE);
    CAPTURE(test_style);

    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE));
    termpaint_attr_set_style(attr.get(), test_style);

    termpaint_surface_clear_rect_with_attr(f.surface, 20, 12, 2, 3, attr);

    checkEmptyPlusSome(f.surface, {
            {{20, 12}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{21, 12}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{20, 13}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{21, 13}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{20, 14}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{21, 14}, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
        },
        singleWideChar("/").withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("clear_rect_with_attr_char") {
    Fixture f{80, 24};
    termpaint_surface_clear_with_char(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN, '/');

    auto test_style = GENERATE(0, TERMPAINT_STYLE_BOLD, TERMPAINT_STYLE_ITALIC, TERMPAINT_STYLE_BLINK,
                               TERMPAINT_STYLE_INVERSE, TERMPAINT_STYLE_STRIKE, TERMPAINT_STYLE_UNDERLINE,
                               TERMPAINT_STYLE_UNDERLINE_DBL, TERMPAINT_STYLE_UNDERLINE_CURLY, TERMPAINT_STYLE_OVERLINE);
    CAPTURE(test_style);

    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE));
    termpaint_attr_set_style(attr.get(), test_style);

    struct TestCase { int ch; std::string s; };
    auto testCase = GENERATE(
                TestCase{' ', " "},
                TestCase{'a', "a"},
                TestCase{'\x7f', TERMPAINT_ERASED},
                TestCase{u'ä', "ä"},
                TestCase{u'あ', TERMPAINT_ERASED},
                TestCase{u'\u0308', TERMPAINT_ERASED});
    CAPTURE(testCase.ch);

    termpaint_surface_clear_rect_with_attr_char(f.surface, 20, 12, 2, 3, attr, testCase.ch);

    checkEmptyPlusSome(f.surface, {
            {{20, 12}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{21, 12}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{20, 13}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{21, 13}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{20, 14}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
            {{21, 14}, singleWideChar(testCase.s).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)
                                .withStyle(test_style)},
        },
        singleWideChar("/").withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("clear rect left totally clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, -1, 3, 1, 2, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
    });
}


TEST_CASE("clear rect left partially clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, -1, 3, 2, 2, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
        {{ 0, 3 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
        {{ 0, 4 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
    });
}


TEST_CASE("clear rect top totally clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, 5, -1, 2, 1, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
    });
}


TEST_CASE("clear rect top partially clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, 5, -1, 2, 2, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
        {{ 5, 0 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
        {{ 6, 0 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
    });
}


TEST_CASE("clear rect right totally clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, 80, 3, 1, 2, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
    });
}


TEST_CASE("clear rect right partially clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, 79, 3, 2, 2, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
        {{ 79, 3 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
        {{ 79, 4 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
    });
}


TEST_CASE("clear rect bottom totally clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, 5, 24, 2, 1, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
    });
}


TEST_CASE("clear rect bottom partially clipped") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_clear_rect(f.surface, 5, 23, 2, 2, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);

    checkEmptyPlusSome(f.surface, {
        {{ 5, 23 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
        {{ 6, 23 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_RED).withBg(TERMPAINT_COLOR_BLUE)},
    });
}


TEST_CASE("soft wrap marker") {
    Fixture f{80, 24};

    struct TestCase { int x; int y; };
    auto testCase = GENERATE(
                TestCase{5, 23},
                TestCase{0, 0},
                TestCase{79, 0},
                TestCase{0, 5});
    CAPTURE(testCase.x);
    CAPTURE(testCase.y);

    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_set_softwrap_marker(f.surface, testCase.x, testCase.y, true);

    checkEmptyPlusSome(f.surface, {
        {{ testCase.x, testCase.y }, singleWideChar(TERMPAINT_ERASED).withSoftWrapMarker()},
    });
}


TEST_CASE("soft wrap marker - removal") {
    Fixture f{80, 24};

    struct TestCase { int x; int y; };
    auto testCase = GENERATE(4);

    CAPTURE(testCase);

    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_set_softwrap_marker(f.surface, 5, 23, true);

    auto expected = singleWideChar(TERMPAINT_ERASED);

    if (testCase == 0) {
        termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    } else if (testCase == 1) {
        termpaint_surface_clear_rect(f.surface, 5, 23, 1, 1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    } else if (testCase == 2) {
        termpaint_surface_write_with_colors(f.surface, 5, 23, TERMPAINT_ERASED, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    } else if (testCase == 3) {
        termpaint_surface_write_with_colors(f.surface, 5, 23, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        expected = singleWideChar(" ");
    } else if (testCase == 4) {
        termpaint_surface_set_softwrap_marker(f.surface, 5, 23, false);
    }

    checkEmptyPlusSome(f.surface, {
        {{ 5, 23 }, expected},
    });
}


TEST_CASE("set fg color") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BOLD);
    termpaint_surface_write_with_attr(f.surface, 3, 3, "A", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_ITALIC);
    termpaint_surface_write_with_attr(f.surface, 4, 3, "B", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BLINK);
    termpaint_surface_write_with_attr(f.surface, 5, 3, "C", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_INVERSE);
    termpaint_surface_write_with_attr(f.surface, 6, 3, "D", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_STRIKE);
    termpaint_surface_write_with_attr(f.surface, 7, 3, "E", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE);
    termpaint_surface_write_with_attr(f.surface, 8, 3, "F", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_DBL);
    termpaint_surface_write_with_attr(f.surface, 9, 3, "G", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_CURLY);
    termpaint_surface_write_with_attr(f.surface, 10, 3, "H", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_OVERLINE);
    termpaint_surface_write_with_attr(f.surface, 11, 3, "I", attr.get());

    for (int i = 0; i < 10; i++) {
        termpaint_surface_set_fg_color(f.surface, 3 + i, 3, TERMPAINT_COLOR_GREEN);
    }

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("A").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 4, 3 }, singleWideChar("B").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_ITALIC)},
        {{ 5, 3 }, singleWideChar("C").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_BLINK)},
        {{ 6, 3 }, singleWideChar("D").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_INVERSE)},
        {{ 7, 3 }, singleWideChar("E").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_STRIKE)},
        {{ 8, 3 }, singleWideChar("F").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE)},
        {{ 9, 3 }, singleWideChar("G").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE_DBL)},
        {{ 10, 3 }, singleWideChar("H").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY)},
        {{ 11, 3 }, singleWideChar("I").withFg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_OVERLINE)},
        {{ 12, 3 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_GREEN)},
    });
}


TEST_CASE("set bg color") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BOLD);
    termpaint_surface_write_with_attr(f.surface, 3, 3, "A", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_ITALIC);
    termpaint_surface_write_with_attr(f.surface, 4, 3, "B", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BLINK);
    termpaint_surface_write_with_attr(f.surface, 5, 3, "C", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_INVERSE);
    termpaint_surface_write_with_attr(f.surface, 6, 3, "D", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_STRIKE);
    termpaint_surface_write_with_attr(f.surface, 7, 3, "E", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE);
    termpaint_surface_write_with_attr(f.surface, 8, 3, "F", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_DBL);
    termpaint_surface_write_with_attr(f.surface, 9, 3, "G", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_CURLY);
    termpaint_surface_write_with_attr(f.surface, 10, 3, "H", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_OVERLINE);
    termpaint_surface_write_with_attr(f.surface, 11, 3, "I", attr.get());

    for (int i = 0; i < 10; i++) {
        termpaint_surface_set_bg_color(f.surface, 3 + i, 3, TERMPAINT_COLOR_GREEN);
    }

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("A").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 4, 3 }, singleWideChar("B").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_ITALIC)},
        {{ 5, 3 }, singleWideChar("C").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_BLINK)},
        {{ 6, 3 }, singleWideChar("D").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_INVERSE)},
        {{ 7, 3 }, singleWideChar("E").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_STRIKE)},
        {{ 8, 3 }, singleWideChar("F").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE)},
        {{ 9, 3 }, singleWideChar("G").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE_DBL)},
        {{ 10, 3 }, singleWideChar("H").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY)},
        {{ 11, 3 }, singleWideChar("I").withBg(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_OVERLINE)},
        {{ 12, 3 }, singleWideChar(TERMPAINT_ERASED).withBg(TERMPAINT_COLOR_GREEN)},
    });
}


TEST_CASE("set deco color") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BOLD);
    termpaint_surface_write_with_attr(f.surface, 3, 3, "A", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_ITALIC);
    termpaint_surface_write_with_attr(f.surface, 4, 3, "B", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BLINK);
    termpaint_surface_write_with_attr(f.surface, 5, 3, "C", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_INVERSE);
    termpaint_surface_write_with_attr(f.surface, 6, 3, "D", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_STRIKE);
    termpaint_surface_write_with_attr(f.surface, 7, 3, "E", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE);
    termpaint_surface_write_with_attr(f.surface, 8, 3, "F", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_DBL);
    termpaint_surface_write_with_attr(f.surface, 9, 3, "G", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_CURLY);
    termpaint_surface_write_with_attr(f.surface, 10, 3, "H", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_OVERLINE);
    termpaint_surface_write_with_attr(f.surface, 11, 3, "I", attr.get());

    for (int i = 0; i < 10; i++) {
        termpaint_surface_set_deco_color(f.surface, 3 + i, 3, TERMPAINT_COLOR_GREEN);
    }

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, singleWideChar("A").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 4, 3 }, singleWideChar("B").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_ITALIC)},
        {{ 5, 3 }, singleWideChar("C").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_BLINK)},
        {{ 6, 3 }, singleWideChar("D").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_INVERSE)},
        {{ 7, 3 }, singleWideChar("E").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_STRIKE)},
        {{ 8, 3 }, singleWideChar("F").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE)},
        {{ 9, 3 }, singleWideChar("G").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE_DBL)},
        {{ 10, 3 }, singleWideChar("H").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY)},
        {{ 11, 3 }, singleWideChar("I").withDeco(TERMPAINT_COLOR_GREEN).withStyle(TERMPAINT_STYLE_OVERLINE)},
        {{ 12, 3 }, singleWideChar(TERMPAINT_ERASED).withDeco(TERMPAINT_COLOR_GREEN)},
    });
}


static int some_handle;

TEST_CASE("tint") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE));
    termpaint_attr_set_deco(attr, TERMPAINT_COLOR_LIGHT_GREY);

    //termpaint_surface_clear_rect_with_attr(f.surface, 5, 3, 2, 2, attr);
    termpaint_surface_write_with_attr(f.surface, 5, 3, "  ", attr);
    termpaint_surface_write_with_attr(f.surface, 5, 4, "  ", attr);

    termpaint_surface_tint(f.surface, [] (void *user_data, unsigned *fg, unsigned *bg, unsigned *deco) {
        CHECK(user_data == &some_handle);
        auto identity = [] (bool x) { return x;};
        CHECK(identity(*fg == TERMPAINT_DEFAULT_COLOR || *fg == TERMPAINT_COLOR_RED));
        if (*fg == TERMPAINT_COLOR_RED) {
            *fg = TERMPAINT_COLOR_MAGENTA;
        } else {
            *fg = TERMPAINT_COLOR_YELLOW;
        }
        CHECK(identity(*bg == TERMPAINT_DEFAULT_COLOR || *bg == TERMPAINT_COLOR_BLUE));
        if (*bg == TERMPAINT_COLOR_BLUE) {
            *bg = TERMPAINT_COLOR_GREEN;
        } else {
            *bg = TERMPAINT_COLOR_CYAN;
        }
        CHECK(identity(*deco == TERMPAINT_DEFAULT_COLOR || *deco == TERMPAINT_COLOR_LIGHT_GREY));
        if (*deco == TERMPAINT_COLOR_LIGHT_GREY) {
            *deco = TERMPAINT_COLOR_DARK_GREY;
        } else {
            *deco = TERMPAINT_COLOR_BRIGHT_YELLOW;
        }
    }, &some_handle);

    checkEmptyPlusSome(f.surface, {
        {{ 5, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_MAGENTA).withBg(TERMPAINT_COLOR_GREEN)
                                      .withDeco(TERMPAINT_COLOR_DARK_GREY)},
        {{ 5, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_MAGENTA).withBg(TERMPAINT_COLOR_GREEN)
                                      .withDeco(TERMPAINT_COLOR_DARK_GREY)},
        {{ 6, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_MAGENTA).withBg(TERMPAINT_COLOR_GREEN)
                                      .withDeco(TERMPAINT_COLOR_DARK_GREY)},
        {{ 6, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_MAGENTA).withBg(TERMPAINT_COLOR_GREEN)
                                      .withDeco(TERMPAINT_COLOR_DARK_GREY)},
    },
       singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_YELLOW)
                          .withBg(TERMPAINT_COLOR_CYAN)
                          .withDeco(TERMPAINT_COLOR_BRIGHT_YELLOW));
}


TEST_CASE("char width") {
    Fixture f{80, 24};
    CHECK(termpaint_surface_char_width(f.surface, 'a') == 1);
    CHECK(termpaint_surface_char_width(f.surface, u'が') == 2);
    CHECK(termpaint_surface_char_width(f.surface, u'\u0308') == 0);
}


TEST_CASE("off screen: compare different size") {
    Fixture f{80, 24};

    usurface_ptr s1, s2;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 40, 24));
    s2.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));

    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_clear(s2, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    CHECK_FALSE(termpaint_surface_same_contents(s1, s2));

    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    s2.reset(termpaint_terminal_new_surface(f.terminal, 80, 12));

    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_clear(s2, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
}


TEST_CASE("off screen: compare same object") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 40, 24));

    CHECK(termpaint_surface_same_contents(s1, s1));
}


TEST_CASE("off screen: compare") {
    Fixture f{80, 24};

    usurface_ptr s1, s2;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    s2.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));

    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_clear(s2, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    SECTION("basic case for identical") {
        termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different fg") {
        termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different bg") {
        termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_CYAN);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different text") {
        termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different text") {
        termpaint_surface_write_with_colors(s1, 10, 3, "e", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "e\u0308", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("no normalization") { // don't depend on this, this *could* change, but currently it's this way
        termpaint_surface_write_with_colors(s1, 10, 3, "\u00eb", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "e\u0308", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("case for content and wide characters") {
        termpaint_surface_write_with_colors(s1, 10, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different text, different cell sizes") {
        termpaint_surface_write_with_colors(s1, 10, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        termpaint_surface_write_with_colors(s2, 10, 3, "sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different deco color") {
        uattr_ptr attr;
        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_deco(attr, TERMPAINT_COLOR_CYAN);

        termpaint_surface_write_with_attr(s1, 10, 3, "sample", attr);
        termpaint_surface_write_with_colors(s2, 10, 3, "sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different patch setup, cleanup and optimize") {
        uattr_ptr attr;
        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_patch(attr, true, "asdf", "dfgh");

        termpaint_surface_write_with_attr(s1, 10, 3, "sample", attr);
        termpaint_surface_write_with_colors(s2, 10, 3, "sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different setup") {
        uattr_ptr attr;
        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_patch(attr, true, "asdf", "dfgh");
        termpaint_surface_write_with_attr(s1, 10, 3, "sample", attr);

        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_patch(attr, true, "xxxx", "dfgh");
        termpaint_surface_write_with_attr(s2, 10, 3, "sample", attr);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different cleanup") {
        uattr_ptr attr;

        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_patch(attr, true, "asdf", "dfgh");
        termpaint_surface_write_with_attr(s1, 10, 3, "sample", attr);

        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_patch(attr, true, "asdf", "yyyy");
        termpaint_surface_write_with_attr(s2, 10, 3, "sample", attr);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different optimize") {
        uattr_ptr attr;
        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_patch(attr, true, "asdf", "dfgh");
        termpaint_surface_write_with_attr(s1, 10, 3, "sample", attr);

        attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
        termpaint_attr_set_patch(attr, false, "asdf", "dfgh");
        termpaint_surface_write_with_attr(s2, 10, 3, "sample", attr);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different attributes") {
        auto test_style = GENERATE(TERMPAINT_STYLE_BOLD, TERMPAINT_STYLE_ITALIC, TERMPAINT_STYLE_BLINK,
                                   TERMPAINT_STYLE_INVERSE, TERMPAINT_STYLE_STRIKE, TERMPAINT_STYLE_UNDERLINE,
                                   TERMPAINT_STYLE_UNDERLINE_DBL, TERMPAINT_STYLE_UNDERLINE_CURLY,
                                   TERMPAINT_STYLE_OVERLINE);

        uattr_ptr attr;
        attr.reset(termpaint_attr_new(TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE));

        termpaint_surface_write_with_attr(s1, 10, 3, "sample", attr);

        termpaint_attr_set_style(attr.get(), test_style);
        termpaint_surface_write_with_attr(s2, 10, 3, "sample", attr);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }

    SECTION("different soft wrap marker state") {
        termpaint_surface_set_softwrap_marker(s1, 5, 17, true);

        CHECK_FALSE(termpaint_surface_same_contents(s1, s2));
    }
}


TEST_CASE("attr") {
    Fixture f{80, 24};
    termpaint_surface_clear(f.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    uattr_ptr attr, clone;
    attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));

    Cell expectedCell = singleWideChar("r");

    auto allStyles = TERMPAINT_STYLE_BOLD | TERMPAINT_STYLE_ITALIC | TERMPAINT_STYLE_BLINK
           | TERMPAINT_STYLE_INVERSE | TERMPAINT_STYLE_STRIKE | TERMPAINT_STYLE_UNDERLINE
           | TERMPAINT_STYLE_OVERLINE;

    SECTION("fg red") {
        termpaint_attr_set_fg(attr, TERMPAINT_COLOR_RED);
        expectedCell = expectedCell.withFg(TERMPAINT_COLOR_RED);
    }

    SECTION("bg red") {
        termpaint_attr_set_bg(attr, TERMPAINT_COLOR_RED);
        expectedCell = expectedCell.withBg(TERMPAINT_COLOR_RED);
    }

    SECTION("deco red") {
        termpaint_attr_set_deco(attr, TERMPAINT_COLOR_RED);
        expectedCell = expectedCell.withDeco(TERMPAINT_COLOR_RED);
    }

    SECTION("bold") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_BOLD);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_BOLD);
    }

    SECTION("reset bold") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_BOLD);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset bold") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_BOLD);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_BOLD);
    }

    SECTION("unset bold from all") {
        termpaint_attr_set_style(attr, allStyles);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_BOLD);
        expectedCell = expectedCell.withStyle(allStyles & ~TERMPAINT_STYLE_BOLD);
    }

    SECTION("italic") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_ITALIC);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_ITALIC);
    }

    SECTION("reset italic") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_ITALIC);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset italic") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_ITALIC);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_ITALIC);
    }

    SECTION("unset italic from all") {
        termpaint_attr_set_style(attr, allStyles);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_ITALIC);
        expectedCell = expectedCell.withStyle(allStyles & ~TERMPAINT_STYLE_ITALIC);
    }

    SECTION("blink") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_BLINK);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_BLINK);
    }

    SECTION("reset blink") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_BLINK);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset blink") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_BLINK);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_BLINK);
    }

    SECTION("unset blink from all") {
        termpaint_attr_set_style(attr, allStyles);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_BLINK);
        expectedCell = expectedCell.withStyle(allStyles & ~TERMPAINT_STYLE_BLINK);
    }

    SECTION("inverse") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_INVERSE);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_INVERSE);
    }

    SECTION("reset inverse") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_INVERSE);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset inverse") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_INVERSE);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_INVERSE);
    }

    SECTION("unset inverse from all") {
        termpaint_attr_set_style(attr, allStyles);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_INVERSE);
        expectedCell = expectedCell.withStyle(allStyles & ~TERMPAINT_STYLE_INVERSE);
    }

    SECTION("strike") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_STRIKE);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_STRIKE);
    }

    SECTION("reset strike") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_STRIKE);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset strike") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_STRIKE);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_STRIKE);
    }

    SECTION("unset strike from all") {
        termpaint_attr_set_style(attr, allStyles);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_STRIKE);
        expectedCell = expectedCell.withStyle(allStyles & ~TERMPAINT_STYLE_STRIKE);
    }

    SECTION("underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_UNDERLINE);
    }

    SECTION("reset underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_UNDERLINE);
    }

    SECTION("unset underline from all") {
        termpaint_attr_set_style(attr, allStyles);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_UNDERLINE);
        expectedCell = expectedCell.withStyle(allStyles & ~TERMPAINT_STYLE_UNDERLINE);
    }

    SECTION("double underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE_DBL);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_UNDERLINE_DBL);
    }

    SECTION("reset double underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE_DBL);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset double underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE_DBL);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_UNDERLINE_DBL);
    }

    SECTION("curly underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE_CURLY);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY);
    }

    SECTION("reset curly underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE_CURLY);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset curly underline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE_CURLY);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_UNDERLINE_CURLY);
    }

    SECTION("underline - with conflicting style dbl") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE
                                 | TERMPAINT_STYLE_UNDERLINE_DBL);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_UNDERLINE);
    }

    SECTION("underline - with conflicting styles dbl + curly") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE
                                 | TERMPAINT_STYLE_UNDERLINE_DBL | TERMPAINT_STYLE_UNDERLINE_CURLY);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_UNDERLINE);
    }

    SECTION("underline - with conflicting style curly") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE
                                 | TERMPAINT_STYLE_UNDERLINE_CURLY);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_UNDERLINE);
    }

    SECTION("double underline - with conflicting style curly") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_UNDERLINE_DBL
                                 | TERMPAINT_STYLE_UNDERLINE_CURLY);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_UNDERLINE_DBL);
    }

    SECTION("overline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_OVERLINE);
        expectedCell = expectedCell.withStyle(TERMPAINT_STYLE_OVERLINE);
    }

    SECTION("reset overline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_OVERLINE);
        termpaint_attr_reset_style(attr);
    }

    SECTION("unset overline") {
        termpaint_attr_set_style(attr, TERMPAINT_STYLE_OVERLINE);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_OVERLINE);
    }

    SECTION("unset overline from all") {
        termpaint_attr_set_style(attr, allStyles);
        termpaint_attr_unset_style(attr, TERMPAINT_STYLE_OVERLINE);
        expectedCell = expectedCell.withStyle(allStyles & ~TERMPAINT_STYLE_OVERLINE);
    }

    SECTION("patch no optimize") {
        termpaint_attr_set_patch(attr, false, "blub", "blah");
        expectedCell = expectedCell.withPatch(false, "blub", "blah");
    }

    SECTION("patch optimize") {
        termpaint_attr_set_patch(attr, true, "blub", "blah");
        expectedCell = expectedCell.withPatch(true, "blub", "blah");
    }

    SECTION("unset patch") {
        termpaint_attr_set_patch(attr, true, "blub", "blah");
        termpaint_attr_set_patch(attr, true, nullptr, nullptr);
    }

    clone.reset(termpaint_attr_clone(attr));
    termpaint_surface_write_with_attr(f.surface, 3, 3, "r", clone);

    checkEmptyPlusSome(f.surface, {
        {{ 3, 3 }, expectedCell},
    });
}


TEST_CASE("off-screen: resize") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 40, 24));

    SECTION("negative width") {
        termpaint_surface_resize(s1, -1, 100);

        CHECK(termpaint_surface_width(s1) == 0);
        CHECK(termpaint_surface_height(s1) == 0);
    }

    SECTION("negative height") {
        termpaint_surface_resize(s1, 100, -1);

        CHECK(termpaint_surface_width(s1) == 0);
        CHECK(termpaint_surface_height(s1) == 0);
    }

    SECTION("20x12") {
        termpaint_surface_resize(s1, 20, 12);

        CHECK(termpaint_surface_width(s1) == 20);
        CHECK(termpaint_surface_height(s1) == 12);
    }
}


TEST_CASE("copy - simple") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 9, 3, 8, 1, f.surface, 23, 15, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
            {{ 23, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 24, 15 }, singleWideChar("S").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 25, 15 }, singleWideChar("a").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 26, 15 }, singleWideChar("m").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 27, 15 }, singleWideChar("p").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 28, 15 }, singleWideChar("l").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 29, 15 }, singleWideChar("e").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 30, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - width == 0") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 9, 3, 0, 1, f.surface, 23, 15, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - src.x bigger than source size") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 80, 3, 0, 1, f.surface, 23, 15, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - src.y bigger than source size") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 9, 24, 0, 1, f.surface, 23, 15, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - src.y == -1") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 0, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_write_with_colors(s1, 10, 1, "xxxxxx", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 9, -1, 8, 2, f.surface, 23, 15, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
            {{ 23, 16 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 24, 16 }, singleWideChar("S").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 25, 16 }, singleWideChar("a").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 26, 16 }, singleWideChar("m").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 27, 16 }, singleWideChar("p").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 28, 16 }, singleWideChar("l").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 29, 16 }, singleWideChar("e").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 30, 16 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - dst.y == -1") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 2, "xxxxxx", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 9, 2, 8, 2, f.surface, 23, -1, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
            {{ 23, 0 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 24, 0 }, singleWideChar("S").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 25, 0 }, singleWideChar("a").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 26, 0 }, singleWideChar("m").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 27, 0 }, singleWideChar("p").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 28, 0 }, singleWideChar("l").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 29, 0 }, singleWideChar("e").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 30, 0 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - dst clipping bottom") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 2, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_write_with_colors(s1, 10, 3, "xxxxxx", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 9, 2, 8, 2, f.surface, 23, 23, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
            {{ 23, 23 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 24, 23 }, singleWideChar("S").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 25, 23 }, singleWideChar("a").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 26, 23 }, singleWideChar("m").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 27, 23 }, singleWideChar("p").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 28, 23 }, singleWideChar("l").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 29, 23 }, singleWideChar("e").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 30, 23 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - src clipping bottom") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_write_with_colors(s1, 10, 23, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_write_with_colors(s1, 10, 22, "xxxxxx", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);

    termpaint_surface_copy_rect(s1, 9, 23, 8, 2, f.surface, 23, 23, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
            {{ 23, 23 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 24, 23 }, singleWideChar("S").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 25, 23 }, singleWideChar("a").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 26, 23 }, singleWideChar("m").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 27, 23 }, singleWideChar("p").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 28, 23 }, singleWideChar("l").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 29, 23 }, singleWideChar("e").withFg(TERMPAINT_COLOR_BLUE).withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 30, 23 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - uninit cell") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));

    termpaint_surface_copy_rect(s1, 9, 3, 8, 1, f.surface, 23, 15, tileLeft, tileRight);

    checkEmptyPlusSome(f.surface, {
            {{ 23, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 24, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 25, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 26, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 27, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 28, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 29, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
            {{ 30, 15 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_DEFAULT_COLOR).withBg(TERMPAINT_DEFAULT_COLOR)},
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - chars that get substituted") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 3, 3, "a\004\u00ad\u0088x", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_copy_rect(s1, 0, 0, 80, 24, f.surface, 0, 0, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    auto check = [] (auto& surface) {
        checkEmptyPlusSome(surface, {
            {{ 3, 3 }, singleWideChar("a")},
            {{ 4, 3 }, singleWideChar(" ")},
            {{ 5, 3 }, singleWideChar("-")},
            {{ 6, 3 }, singleWideChar(" ")},
            {{ 7, 3 }, singleWideChar("x")},
        });
    };

    check(s1);
    check(f.surface);
}


TEST_CASE("copy - rgb colors") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_write_with_colors(s1, 3, 3, "r", TERMPAINT_RGB_COLOR(255, 128, 128), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 4, 3, "g", TERMPAINT_RGB_COLOR(128, 255, 128), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 5, 3, "b", TERMPAINT_RGB_COLOR(128, 128, 255), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 3, 4, "r", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(255, 128, 128));
    termpaint_surface_write_with_colors(s1, 4, 4, "g", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(128, 255, 128));
    termpaint_surface_write_with_colors(s1, 5, 4, "b", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(128, 128, 255));

    termpaint_surface_copy_rect(s1, 0, 0, 80, 24, f.surface, 0, 0, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    auto check = [] (auto& surface) {
        checkEmptyPlusSome(surface, {
            {{ 3, 3 }, singleWideChar("r").withFg(TERMPAINT_RGB_COLOR(0xff, 0x80, 0x80))},
            {{ 4, 3 }, singleWideChar("g").withFg(TERMPAINT_RGB_COLOR(0x80, 0xff, 0x80))},
            {{ 5, 3 }, singleWideChar("b").withFg(TERMPAINT_RGB_COLOR(0x80, 0x80, 0xff))},
            {{ 3, 4 }, singleWideChar("r").withBg(TERMPAINT_RGB_COLOR(0xff, 0x80, 0x80))},
            {{ 4, 4 }, singleWideChar("g").withBg(TERMPAINT_RGB_COLOR(0x80, 0xff, 0x80))},
            {{ 5, 4 }, singleWideChar("b").withBg(TERMPAINT_RGB_COLOR(0x80, 0x80, 0xff))},
        });
    };

    check(s1);
    check(f.surface);
}


TEST_CASE("copy - named fg colors") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 3,  3, " ", TERMPAINT_COLOR_BLACK, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 4,  3, " ", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 5,  3, " ", TERMPAINT_COLOR_GREEN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 6,  3, " ", TERMPAINT_COLOR_YELLOW, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 7,  3, " ", TERMPAINT_COLOR_BLUE, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 8,  3, " ", TERMPAINT_COLOR_MAGENTA, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 9,  3, " ", TERMPAINT_COLOR_CYAN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 10, 3, " ", TERMPAINT_COLOR_LIGHT_GREY, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 3,  4, " ", TERMPAINT_COLOR_DARK_GREY, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 4,  4, " ", TERMPAINT_COLOR_BRIGHT_RED, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 5,  4, " ", TERMPAINT_COLOR_BRIGHT_GREEN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 6,  4, " ", TERMPAINT_COLOR_BRIGHT_YELLOW, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 7,  4, " ", TERMPAINT_COLOR_BRIGHT_BLUE, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 8,  4, " ", TERMPAINT_COLOR_BRIGHT_MAGENTA, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 9,  4, " ", TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 10, 4, " ", TERMPAINT_COLOR_WHITE, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_copy_rect(s1, 0, 0, 80, 24, f.surface, 0, 0, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    auto check = [] (auto& surface) {
        checkEmptyPlusSome(surface, {
            {{ 3, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BLACK)},
            {{ 3, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_DARK_GREY)},
            {{ 4, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_RED)},
            {{ 4, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_RED)},
            {{ 5, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_GREEN)},
            {{ 5, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_GREEN)},
            {{ 6, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW)},
            {{ 6, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_YELLOW)},
            {{ 7, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BLUE)},
            {{ 7, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_BLUE)},
            {{ 8, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_MAGENTA)},
            {{ 8, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_MAGENTA)},
            {{ 9, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_CYAN)},
            {{ 9, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN)},
            {{ 10, 3 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_LIGHT_GREY)},
            {{ 10, 4 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_WHITE)},
        });
    };

    check(s1);
    check(f.surface);
}


TEST_CASE("copy - named bg colors") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_write_with_colors(s1, 3,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 4,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_RED);
    termpaint_surface_write_with_colors(s1, 5,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(s1, 6,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_write_with_colors(s1, 7,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BLUE);
    termpaint_surface_write_with_colors(s1, 8,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_MAGENTA);
    termpaint_surface_write_with_colors(s1, 9,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_CYAN);
    termpaint_surface_write_with_colors(s1, 10, 3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_LIGHT_GREY);
    termpaint_surface_write_with_colors(s1, 3,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_DARK_GREY);
    termpaint_surface_write_with_colors(s1, 4,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_write_with_colors(s1, 5,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_GREEN);
    termpaint_surface_write_with_colors(s1, 6,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_YELLOW);
    termpaint_surface_write_with_colors(s1, 7,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_BLUE);
    termpaint_surface_write_with_colors(s1, 8,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_MAGENTA);
    termpaint_surface_write_with_colors(s1, 9,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_CYAN);
    termpaint_surface_write_with_colors(s1, 10, 4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_WHITE);

    termpaint_surface_copy_rect(s1, 0, 0, 80, 24, f.surface, 0, 0, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    auto check = [] (auto& surface) {
        checkEmptyPlusSome(surface, {
            {{ 3, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BLACK)},
            {{ 3, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_DARK_GREY)},
            {{ 4, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_RED)},
            {{ 4, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_RED)},
            {{ 5, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_GREEN)},
            {{ 5, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
            {{ 6, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_YELLOW)},
            {{ 6, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_YELLOW)},
            {{ 7, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BLUE)},
            {{ 7, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_BLUE)},
            {{ 8, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_MAGENTA)},
            {{ 8, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_MAGENTA)},
            {{ 9, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_CYAN)},
            {{ 9, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_BRIGHT_CYAN)},
            {{ 10, 3 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_LIGHT_GREY)},
            {{ 10, 4 }, singleWideChar(" ").withBg(TERMPAINT_COLOR_WHITE)},
        });
    };

    check(s1);
    check(f.surface);
}


TEST_CASE("copy - indexed colors") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_write_with_colors(s1, 3,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 16);
    termpaint_surface_write_with_colors(s1, 4,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 51);
    termpaint_surface_write_with_colors(s1, 5,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 70);
    termpaint_surface_write_with_colors(s1, 6,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 110);
    termpaint_surface_write_with_colors(s1, 7,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 123);
    termpaint_surface_write_with_colors(s1, 8,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 213);
    termpaint_surface_write_with_colors(s1, 9,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 232);
    termpaint_surface_write_with_colors(s1, 10, 3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 255);
    termpaint_surface_write_with_colors(s1, 3,  4, " ", TERMPAINT_INDEXED_COLOR +  16, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 4,  4, " ", TERMPAINT_INDEXED_COLOR +  51, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 5,  4, " ", TERMPAINT_INDEXED_COLOR +  70, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 6,  4, " ", TERMPAINT_INDEXED_COLOR + 110, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 7,  4, " ", TERMPAINT_INDEXED_COLOR + 123, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 8,  4, " ", TERMPAINT_INDEXED_COLOR + 213, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 9,  4, " ", TERMPAINT_INDEXED_COLOR + 232, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(s1, 10, 4, " ", TERMPAINT_INDEXED_COLOR + 255, TERMPAINT_DEFAULT_COLOR);

    termpaint_surface_copy_rect(s1, 0, 0, 80, 24, f.surface, 0, 0, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    auto check = [] (auto& surface) {
        checkEmptyPlusSome(surface, {
            {{ 3, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 16)},
            {{ 3, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 16)},
            {{ 4, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 51)},
            {{ 4, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 51)},
            {{ 5, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 70)},
            {{ 5, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 70)},
            {{ 6, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 110)},
            {{ 6, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 110)},
            {{ 7, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 123)},
            {{ 7, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 123)},
            {{ 8, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 213)},
            {{ 8, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 213)},
            {{ 9, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 232)},
            {{ 9, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 232)},
            {{ 10, 3 }, singleWideChar(" ").withBg(TERMPAINT_INDEXED_COLOR + 255)},
            {{ 10, 4 }, singleWideChar(" ").withFg(TERMPAINT_INDEXED_COLOR + 255)},
        });
    };

    check(s1);
    check(f.surface);
}


TEST_CASE("copy - attributes") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BOLD);
    termpaint_surface_write_with_attr(s1, 3, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_ITALIC);
    termpaint_surface_write_with_attr(s1, 4, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BLINK);
    termpaint_surface_write_with_attr(s1, 5, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_INVERSE);
    termpaint_surface_write_with_attr(s1, 6, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_STRIKE);
    termpaint_surface_write_with_attr(s1, 7, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE);
    termpaint_surface_write_with_attr(s1, 8, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_DBL);
    termpaint_surface_write_with_attr(s1, 9, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_CURLY);
    termpaint_surface_write_with_attr(s1, 10, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_OVERLINE);
    termpaint_surface_write_with_attr(s1, 11, 3, "X", attr.get());

    termpaint_surface_copy_rect(s1, 0, 0, 80, 24, f.surface, 0, 0, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    auto check = [] (auto& surface) {
        checkEmptyPlusSome(surface, {
            {{ 3, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_BOLD)},
            {{ 4, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_ITALIC)},
            {{ 5, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_BLINK)},
            {{ 6, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_INVERSE)},
            {{ 7, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_STRIKE)},
            {{ 8, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE)},
            {{ 9, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_DBL)},
            {{ 10, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY)},
            {{ 11, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_OVERLINE)},
        });
    };

    check(s1);
    check(f.surface);
}


TEST_CASE("copy - simple patch") {
    Fixture f{80, 24};

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_attr* attr_url = termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_attr_set_patch(attr_url, true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\");
    termpaint_surface_write_with_attr(s1, 3, 3, "ABC", attr_url);
    termpaint_attr_free(attr_url);

    termpaint_surface_copy_rect(s1, 0, 0, 80, 24, f.surface, 0, 0, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    auto check = [] (auto& surface) {
        checkEmptyPlusSome(surface, {
            {{ 3, 3 }, singleWideChar("A").withPatch(true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\")},
            {{ 4, 3 }, singleWideChar("B").withPatch(true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\")},
            {{ 5, 3 }, singleWideChar("C").withPatch(true, "\033]8;;http://example.com\033\\", "\033]8;;\033\\")},
        });
    };

    check(s1);
    check(f.surface);
}


TEST_CASE("copy - soft wrap marker") {
    Fixture f{80, 24};

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);

    termpaint_surface_set_softwrap_marker(f.surface, 25, 15, true);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    //termpaint_surface_write_with_colors(s1, 10, 3, "Sample", TERMPAINT_COLOR_BLUE, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_set_softwrap_marker(s1, 10, 3, true);

    termpaint_surface_copy_rect(s1, 9, 3, 8, 1, f.surface, 23, 15, TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    checkEmptyPlusSome(f.surface, {
            {{ 23, 15 }, singleWideChar(TERMPAINT_ERASED)},
            {{ 24, 15 }, singleWideChar(TERMPAINT_ERASED).withSoftWrapMarker()},
            {{ 25, 15 }, singleWideChar(TERMPAINT_ERASED)},
            {{ 26, 15 }, singleWideChar(TERMPAINT_ERASED)},
            {{ 27, 15 }, singleWideChar(TERMPAINT_ERASED)},
            {{ 28, 15 }, singleWideChar(TERMPAINT_ERASED)},
            {{ 29, 15 }, singleWideChar(TERMPAINT_ERASED)},
            {{ 30, 15 }, singleWideChar(TERMPAINT_ERASED)},
        },
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dest (single line)") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 20, 15, "ＡＢＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 8, 1, f.surface, 23, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 20, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 24, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 26, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 28, 15 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 32, 15 }, doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileLeft == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 22, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 23, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 22, 15 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 22, 15 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    if (tileRight == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 30, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 31, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else if (tileRight == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 30, 15 }] = doubleWideChar("ｆ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileRight == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 30, 15 }] = doubleWideChar("Ｆ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dest (five lines)") {
    // first line has double wide on source and destionation
    // second line has double wide on source only
    // third line has double wide on destination only
    // fourth line has no double wide
    // fifth line has double wide on source and destination, but misaligned

    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 20, 15, "ＡＢＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);
    termpaint_surface_write_with_colors(f.surface, 20, 17, "ＡＢＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);
    termpaint_surface_write_with_colors(f.surface, 19, 19, "ＡＢＣＤＥＦＧＨ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);
    termpaint_surface_write_with_colors(s1, 6, 4, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);
    termpaint_surface_write_with_colors(s1, 6, 7, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 8, 5, f.surface, 23, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 20, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 24, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 26, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 28, 15 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 32, 15 }, doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},

        {{ 23, 16 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 24, 16 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 26, 16 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 28, 16 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 30, 16 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},

        {{ 20, 17 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 22, 17 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 23, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 24, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 25, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 26, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 27, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 28, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 29, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 30, 17 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 31, 17 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 32, 17 }, doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},


        {{ 23, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 24, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 25, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 26, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 27, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 28, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 29, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},
        {{ 30, 18 }, singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_WHITE).withBg(TERMPAINT_COLOR_BLACK)},

        {{ 19, 19 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 23, 19 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 24, 19 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 26, 19 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 28, 19 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 30, 19 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 33, 19 }, doubleWideChar("Ｈ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileLeft == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 22, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 23, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 21, 19 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 22, 15 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 22, 16 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 21, 19 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 22, 19 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 22, 15 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 21, 19 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    if (tileRight == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 30, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 31, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 31, 19 }] = doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else if (tileRight == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 30, 15 }] = doubleWideChar("ｆ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 30, 16 }] = doubleWideChar("ｆ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 30, 19 }] = doubleWideChar("ｆ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 32, 19 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else if (tileRight == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 30, 15 }] = doubleWideChar("Ｆ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 31, 19 }] = doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dst with rect width == 1") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 20, 15, "ＡＢＣ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 1, 1, f.surface, 23, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 20, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 24, 15 }, doubleWideChar("Ｃ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileLeft == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 22, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 23, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 22, 15 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 22, 15 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dst with rect width == 1 and misaligned") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 20, 15, "ＡＢＣ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 7, 3, "ａｂｃ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 1, 1, f.surface, 23, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 20, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 22, 15 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileRight == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 23, 15 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 25, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else {
        expectedCells[{ 23, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 24, 15 }] = doubleWideChar("Ｃ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dst and dst.x == 0") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 0, 15, " ＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 8, 1, f.surface, 0, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 0, 15 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 1, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 3, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 5, 15 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 9, 15 }, doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileRight == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 7, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 8, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else if (tileRight == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 7, 15 }] = doubleWideChar("ｆ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileRight == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 7, 15 }] = doubleWideChar("Ｆ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dst and dst.x == -1") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 0, 15, " ＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 8, 3, 9, 1, f.surface, -1, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 0, 15 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 1, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 3, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 5, 15 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 9, 15 }, doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileRight == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 7, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 8, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else if (tileRight == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 7, 15 }] = doubleWideChar("ｆ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileRight == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 7, 15 }] = doubleWideChar("Ｆ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and src.x == -1") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 20, 15, "ＡＢＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 0, 3, "ｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, -1, 3, 8, 1, f.surface, 23, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 20, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 22, 15 }, doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 24, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 26, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 28, 15 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 32, 15 }, doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileRight == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 30, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
        expectedCells[{ 31, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    } else if (tileRight == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 30, 15 }] = doubleWideChar("ｆ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileRight == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 30, 15 }] = doubleWideChar("Ｆ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dst and covering right most column of dst") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 69, 15, "ＡＢＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 8, 1, f.surface, 72, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 69, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 73, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 75, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 77, 15 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 79, 15 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
    };

    if (tileLeft == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 71, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 72, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 71, 15 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 71, 15 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dst and extending beyond the right most column of dst") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 69, 15, "ＡＢＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 80, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 9, 1, f.surface, 72, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 69, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 73, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 75, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 77, 15 }, doubleWideChar("ｅ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 79, 15 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
    };

    if (tileLeft == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 71, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 72, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 71, 15 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 71, 15 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }

    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}


TEST_CASE("copy - double wide on src and dst and extending beyond the right most column of src") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);

    termpaint_surface_clear(f.surface, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(f.surface, 20, 15, "ＡＢＣＤＥＦＧ",
                                        TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_COLOR_BRIGHT_GREEN);

    usurface_ptr s1;
    s1.reset(termpaint_terminal_new_surface(f.terminal, 15, 24));
    termpaint_surface_clear(s1, TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(s1, 6, 3, "ａｂｃｄｅｆｇ", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_MAGENTA);

    termpaint_surface_copy_rect(s1, 9, 3, 8, 1, f.surface, 23, 15, tileLeft, tileRight);

    std::map<std::tuple<int,int>, Cell> expectedCells = {
        {{ 20, 15 }, doubleWideChar("Ａ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 24, 15 }, doubleWideChar("ｃ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 26, 15 }, doubleWideChar("ｄ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 28, 15 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA)},
        {{ 29, 15 }, singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 30, 15 }, doubleWideChar("Ｆ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
        {{ 32, 15 }, doubleWideChar("Ｇ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN)},
    };

    if (tileLeft == TERMPAINT_COPY_NO_TILE) {
        expectedCells[{ 22, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
        expectedCells[{ 23, 15 }] = singleWideChar(" ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PUT) {
        expectedCells[{ 22, 15 }] = doubleWideChar("ｂ").withFg(TERMPAINT_COLOR_YELLOW).withBg(TERMPAINT_COLOR_MAGENTA);
    } else if (tileLeft == TERMPAINT_COPY_TILE_PRESERVE) {
        expectedCells[{ 22, 15 }] = doubleWideChar("Ｂ").withFg(TERMPAINT_COLOR_BRIGHT_CYAN).withBg(TERMPAINT_COLOR_BRIGHT_GREEN);
    }


    checkEmptyPlusSome(f.surface, expectedCells,
        singleWideChar(TERMPAINT_ERASED).withFg(TERMPAINT_COLOR_CYAN).withBg(TERMPAINT_COLOR_GREEN));
}

static void termpaintp_test_surface_copy_rect_same_surface(termpaint_surface *dst_surface, int x, int y, int width, int height,
                                                      int dst_x, int dst_y, int tile_left, int tile_right) {
    // simple robust implementation to compare real implementation against
    auto src_surface = usurface_ptr::take_ownership(termpaint_surface_new_surface(dst_surface, termpaint_surface_width(dst_surface), termpaint_surface_height(dst_surface)));

    termpaint_surface_copy_rect(dst_surface, 0, 0, termpaint_surface_width(dst_surface), termpaint_surface_height(dst_surface),
                                src_surface, 0, 0,
                                TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_NO_TILE);

    termpaint_surface_copy_rect(src_surface, x, y, width, height,
                                dst_surface, dst_x, dst_y,
                                tile_left, tile_right);
}

static void loremipsumify(termpaint_surface *dst_surface) {
    // this is a random mix of normal ascii and full width variants of ascii letters.
    const char* text[] = {
        "Loｒem iｐｓuｍ doｌｏr ｓｉt　aｍｅｔ,　ｃonseｃtｅｔｕｒ　ａｄｉｐｉsicｉ ｅl ",
        "ｉt, sｅｄ　ｅｉｕｓmｏd ｔｅｍｐoｒ iｎｃｉduｎｔ ｕt lａbｏre ｅｔ doloｒe　ma",
        "gnａ　aliｑua.　Ｕt eｎim ａｄ　ｍｉｎｉm ｖeｎｉaｍ,　ｑuｉｓ nostruｄ ｅｘｅr ",
        "ｃitatｉoｎ　uｌlaｍｃｏ lａbｏｒiｓ ｎiｓi ｕt ａliqｕid ｅｘ ea　ｃoｍmodi　ｃ",
        "ｏnｓeｑuａｔ.　Ｑuiｓ aｕtｅ　ｉｕrｅ　rｅpreｈeｎdｅrｉt ｉn vｏｌｕｐｔaｔｅ ",
        "ｖｅｌiｔ esｓe　ｃiｌluｍ doｌｏre ｅｕ fｕｇｉat nｕlla ｐaｒｉatｕr. Excｅｐt",
        "ｅur　sｉnt obｃａｅcaｔ　cｕｐｉditat　ｎon pｒoiｄｅｎt, ｓｕnt　iｎ culpａ　q",
        "ui　ｏｆｆｉｃia ｄｅｓｅｒｕnt moｌｌiｔ　anim ｉd　ｅｓｔ　laｂorum. Ｄｕｉｓ ",
        "　autｅm ｖel ｅum　iｒiｕｒｅ ｄoｌor　ｉn ｈｅnｄrｅｒiｔ　iｎ vｕlpｕtａｔｅ ",
        "ｖelit　ｅssｅ moｌｅｓtｉe　ｃｏnｓeｑuａt,　ｖel　ｉｌｌｕｍ　dolorｅ ｅu feug",
        "ｉａｔ　nｕｌｌａ ｆaｃｉｌｉｓｉｓ ａt ｖｅrｏ　erｏｓ et ａｃcｕmｓａｎ　ｅt　",
        "iｕsｔo　odｉo　ｄｉｇｎｉｓsiｍ　quｉ　ｂlａndｉｔ pｒaｅsｅｎｔ lｕptａtuｍ　 ",
        "ｚｚｒil dｅｌeｎiｔ　ａuguｅ　duiｓ ｄｏｌｏｒe　tｅ　feuｇａｉt ｎullａ　ｆａc",
        "ｉlisi.　Lｏｒeｍ ｉpｓuｍ doｌoｒ　ｓiｔ aｍｅｔ,　ｃｏnsecｔｅｔuer ａdｉpｉs ",
        "ｃing ｅliｔ, seｄ　dｉａm　noｎuｍmｙ ｎｉｂh ｅｕiｓmｏd　tｉnｃidｕｎt　uｔ  ",
        "ｌaoｒeｅt　ｄｏｌorｅ magｎa　alｉｑｕam　eraｔ　ｖｏｌｕｔｐａt. Uｔ　ｗｉsｉ ",
        "ｅnｉｍ ａｄ　ｍｉnｉｍ ｖｅnｉａm, qｕiｓ　ｎostｒｕd eｘｅrｃｉ ｔａtｉoｎ ｕ ",
        "ｌlａｍcｏｒｐｅｒ　sｕscｉｐｉt　lｏｂｏrｔiｓ ｎisｌ　ｕｔ aｌｉｑｕip　ex　ea",
        " cｏmｍｏdo　ｃonsequａt.　Ｄｕｉｓ ａｕtｅｍ ｖｅｌ ｅｕm　ｉｒiｕｒe dｏlｏr  ",
        "ｉn ｈeｎdｒｅｒiｔ　iｎ　vulｐutaｔｅ　vｅｌｉｔ eｓｓｅ ｍolestiｅ ｃｏｎsｅqu",
        "ａt,　ｖel ｉｌluｍ　dolore　eu feugｉat ｎｕlla　ｆａｃｉlｉｓiｓ ａt ｖｅｒｏ ",
        "　eros　et　accｕｍsan ｅt iｕｓｔｏ ｏdｉo dｉgnｉssiｍ　quｉ ｂlaｎｄｉｔ　pr ",
        "ａｅseｎｔ　lｕｐｔａｔuｍ　ｚzｒiｌ　ｄelｅｎｉt augｕe dｕｉｓ dｏｌｏｒe　te ",
        "feuｇaiｔ　nｕｌｌa fａｃiｌｉｓi. Naｍ libeｒ　ｔempor cuｍ　ｓｏluｔａ ｎobiｓ",
    };
    for (int i = 0; i < 24; i++) {
        termpaint_surface_write_with_colors(dst_surface, 0, i, text[i],
                                            TERMPAINT_COLOR_WHITE, TERMPAINT_COLOR_BLACK);
    }
}

TEST_CASE("copy - same surface - same location") {
    Fixture f{80, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);

    // just get some coverage. nothing specific here.
    auto place = [] (int v) {
        if (v < 0) {
            return 80 - v;
        } else {
            return v;
        }
    };

    int x = place(GENERATE(range(-15, 15)));
    int y = GENERATE(0, 8, 16);
    int width = GENERATE(range(0, 10));
    int height = GENERATE(1, 2, 4, 8);
    int dst_x = x;
    int dst_y = y;

    CAPTURE(tileLeft);
    CAPTURE(tileRight);
    CAPTURE(x);
    CAPTURE(width);

    loremipsumify(f.surface);

    auto dup = usurface_ptr::take_ownership(termpaint_surface_duplicate(f.surface));
    CHECK(termpaint_surface_same_contents(f.surface, dup));

    termpaintp_test_surface_copy_rect_same_surface(dup, x, y, width, height,
                                                   dst_x, dst_y,
                                                   tileLeft, tileRight);

    termpaint_surface_copy_rect(f.surface, x, y, width, height,
                                f.surface, dst_x, dst_y,
                                tileLeft, tileRight);

    CHECK(termpaint_surface_same_contents(f.surface, dup));
}

TEST_CASE("copy - same surface") {
    Fixture f{40, 24};

    auto tileLeft = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);
    auto tileRight = GENERATE(TERMPAINT_COPY_NO_TILE, TERMPAINT_COPY_TILE_PUT, TERMPAINT_COPY_TILE_PRESERVE);


    // just get some coverage. nothing specific here.
    auto place = [] (int v) {
        if (v < 0) {
            return 40 - v;
        } else {
            return v;
        }
    };

    int x = place(GENERATE(range(-10, 10)));
    int y = 8;
    int width = GENERATE(1, 2, 5);
    int height = 4;
    int dst_x = x + GENERATE(range(-7, 3));
    int dst_y = GENERATE(0, 8, 16);

    CAPTURE(tileLeft);
    CAPTURE(tileRight);
    CAPTURE(x);
    CAPTURE(width);

    loremipsumify(f.surface);

    auto dup = usurface_ptr::take_ownership(termpaint_surface_duplicate(f.surface));
    CHECK(termpaint_surface_same_contents(f.surface, dup));

    termpaintp_test_surface_copy_rect_same_surface(dup, x, y, width, height,
                                                   dst_x, dst_y,
                                                   tileLeft, tileRight);

    termpaint_surface_copy_rect(f.surface, x, y, width, height,
                                f.surface, dst_x, dst_y,
                                tileLeft, tileRight);

    CHECK(termpaint_surface_same_contents(f.surface, dup));
}

// internal but exposed
extern "C" {
    bool termpaintp_test();
}

TEST_CASE("termpaintp_test") {
    bool x = termpaintp_test();
    CHECK(x);
}
