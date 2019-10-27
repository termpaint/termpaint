#include "../third-party/catch.hpp"

#include "../termpaint.h"
#include "../termpaintx.h"

#include "terminaloutput.h"

#include <map>

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
};

using uattr_ptr = unique_cptr<termpaint_attr, termpaint_attr_free>;

struct SimpleFullscreen {
    SimpleFullscreen(bool altScreen = true) {
        resetAndClear();

        integration = termpaintx_full_integration_from_fd(1, 0, "+kbdsigint +kbdsigtstp");
        REQUIRE(integration);

        terminal.reset(termpaint_terminal_new(integration));
        REQUIRE(terminal);

        termpaintx_full_integration_set_terminal(integration, terminal);
        surface = termpaint_terminal_get_surface(terminal);
        REQUIRE(surface);
        termpaint_terminal_set_event_cb(terminal, [] (void *, termpaint_event*) {}, nullptr);
        termpaint_terminal_auto_detect(terminal);
        termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                               "Terminal auto detection is taking unusually long, press space to abort.");

        if (altScreen) {
            termpaint_terminal_setup_fullscreen(terminal, 80, 24, "+kbdsig");
        } else {
            termpaint_terminal_setup_fullscreen(terminal, 80, 24, "+kbdsig -altscreen");
        }
    }

    unique_cptr<termpaint_terminal, termpaint_terminal_free_with_restore> terminal;
    termpaint_surface *surface;
    termpaint_integration *integration;
};

void checkEmptyPlusSome(const CapturedState &s, const std::map<std::tuple<int,int>, CapturedCell> &some) {
    CHECK(s.width == 80);
    CHECK(s.height == 24);
    CHECK(s.altScreen == true);
    for (const CapturedRow& row: s.rows) {
        for (const CapturedCell& cell: row.cells) {
            CAPTURE(cell.x);
            CAPTURE(cell.y);
            if (some.count({cell.x, cell.y})) {
                auto& expected = some.at({cell.x, cell.y});
                CHECK(cell.bg == expected.bg);
                CHECK(cell.fg == expected.fg);
                CHECK(cell.deco == expected.deco);
                CHECK(cell.data == expected.data);
                CHECK(cell.style == expected.style);
                CHECK(cell.width == expected.width);
            } else {
                CHECK(cell.bg == "");
                CHECK(cell.fg == "");
                CHECK(cell.deco == "");
                CHECK(cell.data == " ");
                CHECK(cell.style == 0);
                CHECK(cell.width == 1);
            }
        }
    }
}

CapturedCell simpleChar(std::string ch) {
    CapturedCell c;
    c.data = ch;
    return c;
}

CapturedCell doubleWidthChar(std::string ch) {
    CapturedCell c;
    c.data = ch;
    c.width = 2;
    return c;
}

TEST_CASE("empty") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();
    checkEmptyPlusSome(s, {});

    CHECK(s.altScreen == true);
    CHECK(s.invScreen == false);

}

TEST_CASE("simple text") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 10, 3 }, simpleChar("S")},
        {{ 11, 3 }, simpleChar("a")},
        {{ 12, 3 }, simpleChar("m")},
        {{ 13, 3 }, simpleChar("p")},
        {{ 14, 3 }, simpleChar("l")},
        {{ 15, 3 }, simpleChar("e")},
    });
}

TEST_CASE("double width") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, doubleWidthChar("あ")},
        {{ 5, 3 }, doubleWidthChar("え")},
    });
}

TEST_CASE("chars that get substituted") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "a\004\u00ad\u0088x", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar("a")},
        {{ 4, 3 }, simpleChar(" ")},
        {{ 5, 3 }, simpleChar("-")},
        {{ 6, 3 }, simpleChar(" ")},
        {{ 7, 3 }, simpleChar("x")},
    });
}

TEST_CASE("vanish chars") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "あえ", TERMPAINT_NAMED_COLOR + 1, TERMPAINT_NAMED_COLOR + 2);

    termpaint_surface_write_with_colors(t.surface, 4, 3, "ab", TERMPAINT_NAMED_COLOR + 3, TERMPAINT_NAMED_COLOR + 4);

    termpaint_terminal_flush(t.terminal, true);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar(" ").withBg("green").withFg("red")},
        {{ 4, 3 }, simpleChar("a").withBg("blue").withFg("yellow")},
        {{ 5, 3 }, simpleChar("b").withBg("blue").withFg("yellow")},
        {{ 6, 3 }, simpleChar(" ").withBg("green").withFg("red")},
    });
}

TEST_CASE("vanish chars - incremental") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "あえ", TERMPAINT_NAMED_COLOR + 1, TERMPAINT_NAMED_COLOR + 2);

    termpaint_terminal_flush(t.terminal, false);

    termpaint_surface_write_with_colors(t.surface, 4, 3, "ab", TERMPAINT_NAMED_COLOR + 3, TERMPAINT_NAMED_COLOR + 4);

    termpaint_terminal_flush(t.terminal, true);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar(" ").withBg("green").withFg("red")},
        {{ 4, 3 }, simpleChar("a").withBg("blue").withFg("yellow")},
        {{ 5, 3 }, simpleChar("b").withBg("blue").withFg("yellow")},
        {{ 6, 3 }, simpleChar(" ").withBg("green").withFg("red")},
    });
}

TEST_CASE("rgb colors") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "r", TERMPAINT_RGB_COLOR(255, 128, 128), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 4, 3, "g", TERMPAINT_RGB_COLOR(128, 255, 128), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 5, 3, "b", TERMPAINT_RGB_COLOR(128, 128, 255), TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 4, "r", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(255, 128, 128));
    termpaint_surface_write_with_colors(t.surface, 4, 4, "g", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(128, 255, 128));
    termpaint_surface_write_with_colors(t.surface, 5, 4, "b", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(128, 128, 255));

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar("r").withFg("#ff8080")},
        {{ 4, 3 }, simpleChar("g").withFg("#80ff80")},
        {{ 5, 3 }, simpleChar("b").withFg("#8080ff")},
        {{ 3, 4 }, simpleChar("r").withBg("#ff8080")},
        {{ 4, 4 }, simpleChar("g").withBg("#80ff80")},
        {{ 5, 4 }, simpleChar("b").withBg("#8080ff")},
    });
}

TEST_CASE("named fg colors") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3,  3, " ", TERMPAINT_NAMED_COLOR +  0, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 4,  3, " ", TERMPAINT_NAMED_COLOR +  1, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 5,  3, " ", TERMPAINT_NAMED_COLOR +  2, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 6,  3, " ", TERMPAINT_NAMED_COLOR +  3, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 7,  3, " ", TERMPAINT_NAMED_COLOR +  4, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 8,  3, " ", TERMPAINT_NAMED_COLOR +  5, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 9,  3, " ", TERMPAINT_NAMED_COLOR +  6, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 10, 3, " ", TERMPAINT_NAMED_COLOR +  7, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3,  4, " ", TERMPAINT_NAMED_COLOR +  8, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 4,  4, " ", TERMPAINT_NAMED_COLOR +  9, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 5,  4, " ", TERMPAINT_NAMED_COLOR + 10, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 6,  4, " ", TERMPAINT_NAMED_COLOR + 11, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 7,  4, " ", TERMPAINT_NAMED_COLOR + 12, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 8,  4, " ", TERMPAINT_NAMED_COLOR + 13, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 9,  4, " ", TERMPAINT_NAMED_COLOR + 14, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 10, 4, " ", TERMPAINT_NAMED_COLOR + 15, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar(" ").withFg("black")},
        {{ 3, 4 }, simpleChar(" ").withFg("bright black")},
        {{ 4, 3 }, simpleChar(" ").withFg("red")},
        {{ 4, 4 }, simpleChar(" ").withFg("bright red")},
        {{ 5, 3 }, simpleChar(" ").withFg("green")},
        {{ 5, 4 }, simpleChar(" ").withFg("bright green")},
        {{ 6, 3 }, simpleChar(" ").withFg("yellow")},
        {{ 6, 4 }, simpleChar(" ").withFg("bright yellow")},
        {{ 7, 3 }, simpleChar(" ").withFg("blue")},
        {{ 7, 4 }, simpleChar(" ").withFg("bright blue")},
        {{ 8, 3 }, simpleChar(" ").withFg("magenta")},
        {{ 8, 4 }, simpleChar(" ").withFg("bright magenta")},
        {{ 9, 3 }, simpleChar(" ").withFg("cyan")},
        {{ 9, 4 }, simpleChar(" ").withFg("bright cyan")},
        {{ 10, 3 }, simpleChar(" ").withFg("white")},
        {{ 10, 4 }, simpleChar(" ").withFg("bright white")},
    });
}

TEST_CASE("named bg colors") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  0);
    termpaint_surface_write_with_colors(t.surface, 4,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  1);
    termpaint_surface_write_with_colors(t.surface, 5,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  2);
    termpaint_surface_write_with_colors(t.surface, 6,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  3);
    termpaint_surface_write_with_colors(t.surface, 7,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  4);
    termpaint_surface_write_with_colors(t.surface, 8,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  5);
    termpaint_surface_write_with_colors(t.surface, 9,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  6);
    termpaint_surface_write_with_colors(t.surface, 10, 3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  7);
    termpaint_surface_write_with_colors(t.surface, 3,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  8);
    termpaint_surface_write_with_colors(t.surface, 4,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR +  9);
    termpaint_surface_write_with_colors(t.surface, 5,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR + 10);
    termpaint_surface_write_with_colors(t.surface, 6,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR + 11);
    termpaint_surface_write_with_colors(t.surface, 7,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR + 12);
    termpaint_surface_write_with_colors(t.surface, 8,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR + 13);
    termpaint_surface_write_with_colors(t.surface, 9,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR + 14);
    termpaint_surface_write_with_colors(t.surface, 10, 4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_NAMED_COLOR + 15);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar(" ").withBg("black")},
        {{ 3, 4 }, simpleChar(" ").withBg("bright black")},
        {{ 4, 3 }, simpleChar(" ").withBg("red")},
        {{ 4, 4 }, simpleChar(" ").withBg("bright red")},
        {{ 5, 3 }, simpleChar(" ").withBg("green")},
        {{ 5, 4 }, simpleChar(" ").withBg("bright green")},
        {{ 6, 3 }, simpleChar(" ").withBg("yellow")},
        {{ 6, 4 }, simpleChar(" ").withBg("bright yellow")},
        {{ 7, 3 }, simpleChar(" ").withBg("blue")},
        {{ 7, 4 }, simpleChar(" ").withBg("bright blue")},
        {{ 8, 3 }, simpleChar(" ").withBg("magenta")},
        {{ 8, 4 }, simpleChar(" ").withBg("bright magenta")},
        {{ 9, 3 }, simpleChar(" ").withBg("cyan")},
        {{ 9, 4 }, simpleChar(" ").withBg("bright cyan")},
        {{ 10, 3 }, simpleChar(" ").withBg("white")},
        {{ 10, 4 }, simpleChar(" ").withBg("bright white")},
    });
}

TEST_CASE("indexed colors") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 16);
    termpaint_surface_write_with_colors(t.surface, 4,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 51);
    termpaint_surface_write_with_colors(t.surface, 5,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 70);
    termpaint_surface_write_with_colors(t.surface, 6,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 110);
    termpaint_surface_write_with_colors(t.surface, 7,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 123);
    termpaint_surface_write_with_colors(t.surface, 8,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 213);
    termpaint_surface_write_with_colors(t.surface, 9,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 232);
    termpaint_surface_write_with_colors(t.surface, 10, 3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_INDEXED_COLOR + 255);
    termpaint_surface_write_with_colors(t.surface, 3,  4, " ", TERMPAINT_INDEXED_COLOR +  16, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 4,  4, " ", TERMPAINT_INDEXED_COLOR +  51, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 5,  4, " ", TERMPAINT_INDEXED_COLOR +  70, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 6,  4, " ", TERMPAINT_INDEXED_COLOR + 110, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 7,  4, " ", TERMPAINT_INDEXED_COLOR + 123, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 8,  4, " ", TERMPAINT_INDEXED_COLOR + 213, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 9,  4, " ", TERMPAINT_INDEXED_COLOR + 232, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 10, 4, " ", TERMPAINT_INDEXED_COLOR + 255, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar(" ").withBg("16")},
        {{ 3, 4 }, simpleChar(" ").withFg("16")},
        {{ 4, 3 }, simpleChar(" ").withBg("51")},
        {{ 4, 4 }, simpleChar(" ").withFg("51")},
        {{ 5, 3 }, simpleChar(" ").withBg("70")},
        {{ 5, 4 }, simpleChar(" ").withFg("70")},
        {{ 6, 3 }, simpleChar(" ").withBg("110")},
        {{ 6, 4 }, simpleChar(" ").withFg("110")},
        {{ 7, 3 }, simpleChar(" ").withBg("123")},
        {{ 7, 4 }, simpleChar(" ").withFg("123")},
        {{ 8, 3 }, simpleChar(" ").withBg("213")},
        {{ 8, 4 }, simpleChar(" ").withFg("213")},
        {{ 9, 3 }, simpleChar(" ").withBg("232")},
        {{ 9, 4 }, simpleChar(" ").withFg("232")},
        {{ 10, 3 }, simpleChar(" ").withBg("255")},
        {{ 10, 4 }, simpleChar(" ").withFg("255")},
    });
}

TEST_CASE("attributes") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    uattr_ptr attr;
    attr.reset(termpaint_attr_new(TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR));
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BOLD);
    termpaint_surface_write_with_attr(t.surface, 3, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_ITALIC);
    termpaint_surface_write_with_attr(t.surface, 4, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_BLINK);
    termpaint_surface_write_with_attr(t.surface, 5, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_INVERSE);
    termpaint_surface_write_with_attr(t.surface, 6, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_STRIKE);
    termpaint_surface_write_with_attr(t.surface, 7, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE);
    termpaint_surface_write_with_attr(t.surface, 8, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_DBL);
    termpaint_surface_write_with_attr(t.surface, 9, 3, "X", attr.get());
    termpaint_attr_reset_style(attr.get());
    termpaint_attr_set_style(attr.get(), TERMPAINT_STYLE_UNDERLINE_CURLY);
    termpaint_surface_write_with_attr(t.surface, 10, 3, "X", attr.get());

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 4, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_ITALIC)},
        {{ 5, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_BLINK)},
        {{ 6, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_INVERSE)},
        {{ 7, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_STRIKE)},
        {{ 8, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE)},
        {{ 9, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_DBL)},
        {{ 10, 3 }, simpleChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY)},
    });
}

TEST_CASE("mouse mode: clicks") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_mouse_mode(t.terminal, TERMPAINT_MOUSE_MODE_CLICKS);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {});

    CHECK(s.mouseMode == "clicks");

    termpaint_terminal_set_mouse_mode(t.terminal, TERMPAINT_MOUSE_MODE_OFF);
    s = capture();
    CHECK(s.mouseMode == "");
}

TEST_CASE("mouse mode: drag") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_mouse_mode(t.terminal, TERMPAINT_MOUSE_MODE_DRAG);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {});

    CHECK(s.mouseMode == "drag");
}

TEST_CASE("mouse mode: movement") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_mouse_mode(t.terminal, TERMPAINT_MOUSE_MODE_MOVEMENT);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {});

    CHECK(s.mouseMode == "movement");
}

TEST_CASE("cursor position default") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorX == 79);
    CHECK(s.cursorY == 23);
}

TEST_CASE("cursor position set") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorX == 14);
    CHECK(s.cursorY == 4);
}


TEST_CASE("cursor visibility default") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorVisible == true);
}


TEST_CASE("cursor show") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_set_cursor_visible(t.terminal, true);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorVisible == true);
}

TEST_CASE("cursor hide") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_set_cursor_visible(t.terminal, false);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorVisible == false);
}

TEST_CASE("cursor blink") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_set_cursor_style(t.terminal, TERMPAINT_CURSOR_STYLE_TERM_DEFAULT, true);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorBlink == true);
}

TEST_CASE("cursor no blink") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_set_cursor_style(t.terminal, TERMPAINT_CURSOR_STYLE_BLOCK, false);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorBlink == false);
}

TEST_CASE("cursor shape block") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_set_cursor_style(t.terminal, TERMPAINT_CURSOR_STYLE_BLOCK, false);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorShape == "block");
}

#include <unistd.h>

TEST_CASE("cursor shape bar") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_set_cursor_style(t.terminal, TERMPAINT_CURSOR_STYLE_BAR, false);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorShape == "bar");
}

TEST_CASE("cursor shape underline") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_cursor_position(t.terminal, 14, 4);
    termpaint_terminal_set_cursor_style(t.terminal, TERMPAINT_CURSOR_STYLE_UNDERLINE, false);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.cursorShape == "underline");
}

TEST_CASE("title") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_title(t.terminal, "fancy title", TERMPAINT_TITLE_MODE_PREFER_RESTORE);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.title == "fancy title");
}

TEST_CASE("title with chars that get substituted") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_title(t.terminal, "fancy\005\u00ad\u0087title", TERMPAINT_TITLE_MODE_PREFER_RESTORE);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.title == "fancy - title");
}

TEST_CASE("icon title") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_icon_title(t.terminal, "fancy title", TERMPAINT_TITLE_MODE_PREFER_RESTORE);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    CHECK(s.iconTitle == "fancy title");
}

TEST_CASE("no alt screen") {
    SimpleFullscreen t { false };
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();
    CHECK(s.altScreen == false);
}


TEST_CASE("basic pause") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();
    checkEmptyPlusSome(s, {});

    CHECK(s.altScreen == true);
    CHECK(s.invScreen == false);

    termpaint_terminal_pause(t.terminal);

    s = capture();
    CHECK(s.altScreen == false);

    termpaint_terminal_unpause(t.terminal);
    termpaint_terminal_flush(t.terminal, false);

    s = capture();
    checkEmptyPlusSome(s, {});

    CHECK(s.altScreen == true);
}

TEST_CASE("bell") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_flush(t.terminal, false);
    termpaint_terminal_bell(t.terminal);

    CapturedState s = capture();
    checkEmptyPlusSome(s, {});

    CHECK(*asyncQueue.pop() == "*bell");
}
