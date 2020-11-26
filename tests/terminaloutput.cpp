// SPDX-License-Identifier: BSL-1.0
#include "../third-party/catch.hpp"

#include "../termpaint.h"
#include "../termpaintx.h"

#include "terminaloutput.h"

#include <algorithm>
#include <map>
#include <vector>

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

class Overrides {
public:
    Overrides withSoftWrappedLines(std::vector<int> lines) {
        softWrappedLines = lines;
        return *this;
    }

public:
    std::vector<int> softWrappedLines;
};

void checkEmptyPlusSome(const CapturedState &s, const std::map<std::tuple<int,int>, CapturedCell> &some,
                        const Overrides overrides = {}) {
    CHECK(s.width == 80);
    CHECK(s.height == 24);
    CHECK(s.altScreen == true);
    for (const CapturedRow& row: s.rows) {
        {
            INFO("y = " << row.cells[0].y);
            bool expectedSoftWrapped = std::find(overrides.softWrappedLines.begin(),
                                                 overrides.softWrappedLines.end(),
                                                 row.cells[0].y) != overrides.softWrappedLines.end();
            CHECK(row.softWrapped == expectedSoftWrapped);
        }
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
                CHECK(cell.erased == expected.erased);
            } else {
                bool expectErased = true;
                for (int i = cell.x + 1; i < s.width; i++) {
                    // terminals only track trailing erased cells
                    if (some.count({i, cell.y}) && !some.at({i, cell.y}).erased) {
                        expectErased = false;
                        break;
                    }
                }
                CHECK(cell.bg == "");
                CHECK(cell.fg == "");
                CHECK(cell.deco == "");
                CHECK(cell.data == " ");
                CHECK(cell.style == 0);
                CHECK(cell.width == 1);
                CHECK(cell.erased == expectErased);
            }
        }
    }
}

CapturedCell singleWideChar(std::string ch) {
    CapturedCell c;
    c.data = ch;
    return c;
}

CapturedCell doubleWideChar(std::string ch) {
    CapturedCell c;
    c.data = ch;
    c.width = 2;
    return c;
}


TEST_CASE("no init") {
    SimpleFullscreen t;
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();
    checkEmptyPlusSome(s, {});

    CHECK(s.altScreen == true);
    CHECK(s.invScreen == false);

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


TEST_CASE("restore") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);
    termpaint_terminal_flush(t.terminal, false);

    t.terminal.reset();

    CapturedState s = capture();

    CHECK(s.altScreen == false);
    CHECK(s.invScreen == false);

    CHECK(s.sgrState.style == 0);
    CHECK(s.sgrState.fg == std::string());
    CHECK(s.sgrState.bg == std::string());
    CHECK(s.sgrState.deco == std::string());
}


TEST_CASE("restore - no fullscreen") {
    resetAndClear();

    termpaint_integration *integration = termpaintx_full_integration_from_fd(1, 0, "+kbdsigint +kbdsigtstp");
    REQUIRE(integration);

    unique_cptr<termpaint_terminal, termpaint_terminal_free_with_restore> terminal;
    terminal.reset(termpaint_terminal_new(integration));
    REQUIRE(terminal);

    termpaintx_full_integration_set_terminal(integration, terminal);
    termpaint_surface *surface = termpaint_terminal_get_surface(terminal);
    REQUIRE(surface);
    termpaint_terminal_set_event_cb(terminal, [] (void *, termpaint_event*) {}, nullptr);
    termpaint_terminal_auto_detect(terminal);
    termpaintx_full_integration_wait_for_ready_with_message(integration, 10000,
                                           "Terminal auto detection is taking unusually long, press space to abort.");


    termpaint_surface_clear(surface, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);
    termpaint_terminal_flush(terminal, false);

    terminal.reset();

    CapturedState s = capture();

    CHECK(s.altScreen == false);
    CHECK(s.invScreen == false);

    CHECK(s.sgrState.style == 0);
    CHECK(s.sgrState.fg == std::string());
    CHECK(s.sgrState.bg == std::string());
    CHECK(s.sgrState.deco == std::string());
}


TEST_CASE("simple text") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 10, 3, "Sample", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 10, 3 }, singleWideChar("S")},
        {{ 11, 3 }, singleWideChar("a")},
        {{ 12, 3 }, singleWideChar("m")},
        {{ 13, 3 }, singleWideChar("p")},
        {{ 14, 3 }, singleWideChar("l")},
        {{ 15, 3 }, singleWideChar("e")},
    });
}

TEST_CASE("double width") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "あえ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, doubleWideChar("あ")},
        {{ 5, 3 }, doubleWideChar("え")},
    });
}

TEST_CASE("chars that get substituted") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "a\004\u00ad\u0088x", TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, singleWideChar("a")},
        {{ 4, 3 }, singleWideChar(" ")},
        {{ 5, 3 }, singleWideChar("-")},
        {{ 6, 3 }, singleWideChar(" ")},
        {{ 7, 3 }, singleWideChar("x")},
    });
}

TEST_CASE("vanish chars") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_COLOR_GREEN);

    termpaint_surface_write_with_colors(t.surface, 4, 3, "ab", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_BLUE);

    termpaint_terminal_flush(t.terminal, true);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, singleWideChar(" ").withBg("green").withFg("red")},
        {{ 4, 3 }, singleWideChar("a").withBg("blue").withFg("yellow")},
        {{ 5, 3 }, singleWideChar("b").withBg("blue").withFg("yellow")},
        {{ 6, 3 }, singleWideChar(" ").withBg("green").withFg("red")},
    });
}

TEST_CASE("vanish chars - incremental") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3, 3, "あえ", TERMPAINT_COLOR_RED, TERMPAINT_COLOR_GREEN);

    termpaint_terminal_flush(t.terminal, false);

    termpaint_surface_write_with_colors(t.surface, 4, 3, "ab", TERMPAINT_COLOR_YELLOW, TERMPAINT_COLOR_BLUE);

    termpaint_terminal_flush(t.terminal, true);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, singleWideChar(" ").withBg("green").withFg("red")},
        {{ 4, 3 }, singleWideChar("a").withBg("blue").withFg("yellow")},
        {{ 5, 3 }, singleWideChar("b").withBg("blue").withFg("yellow")},
        {{ 6, 3 }, singleWideChar(" ").withBg("green").withFg("red")},
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
        {{ 3, 3 }, singleWideChar("r").withFg("#ff8080")},
        {{ 4, 3 }, singleWideChar("g").withFg("#80ff80")},
        {{ 5, 3 }, singleWideChar("b").withFg("#8080ff")},
        {{ 3, 4 }, singleWideChar("r").withBg("#ff8080")},
        {{ 4, 4 }, singleWideChar("g").withBg("#80ff80")},
        {{ 5, 4 }, singleWideChar("b").withBg("#8080ff")},
    });
}

TEST_CASE("rgb colors with quantize to 256 colors - grey, misc and grid points") {
    SimpleFullscreen t;
    termpaint_terminal_disable_capability(t.terminal, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    // test that colors of the from (i,i,i) are converted to the nearest color on the grey ramp
    // or the nearest color from the 6x6x6 cube.
    // for each (except first and last) palette color there is one entry for the lower bound,
    // one entry for the palette color itself and one entry for the upper bound.
    const std::initializer_list<std::tuple<std::string, int>> greyMain = {
        {"16", 0}, {"16", 4}, {"232", 5}, {"232", 8}, {"232", 12},
        {"233", 13}, {"233", 18}, {"233", 22}, {"234", 23}, {"234", 28}, {"234", 32},
        {"235", 33}, {"235", 38}, {"235", 42}, {"236", 43}, {"236", 48}, {"236", 52},
        {"237", 53}, {"237", 58}, {"237", 62}, {"238", 63}, {"238", 68}, {"238", 72},
        {"239", 73}, {"239", 78}, {"239", 82}, {"240", 83}, {"240", 88}, {"240", 91},
        {"59", 92}, {"59", 95}, {"59", 96}, // 16 + 36 + 6 + 1
        {"241", 97}, {"241", 98}, {"241", 102}, {"242", 103}, {"242", 108}, {"242", 112},
        {"243", 113}, {"243", 118}, {"243", 122}, {"244", 123}, {"244", 128}, {"244", 131},
        {"102", 132}, {"102", 135}, {"102", 136}, // 16 + 2*36 + 2*6 + 2*1
        {"245", 137}, {"245", 138}, {"245", 142}, {"246", 143}, {"246", 148}, {"246", 152},
        {"247", 153}, {"247", 158}, {"247", 162}, {"248", 163}, {"248", 168}, {"248", 171},
        {"145", 172}, {"145", 175}, {"145", 176}, // 16 + 3*36 + 3*6 + 3*1
        {"249", 177}, {"249", 178}, {"249", 182}, {"250", 183}, {"250", 188}, {"250", 192},
        {"251", 193}, {"251", 198}, {"251", 202}, {"252", 203}, {"252", 208}, {"252", 211},
        {"188", 212}, {"188", 215}, {"188", 216}, // 16 + 4*36 + 4*6 + 4*1
        {"253", 217}, {"253", 218}, {"253", 222}, {"254", 223}, {"254", 228}, {"254", 232},
        {"255", 233}, {"255", 238}, {"255", 246},
        {"231", 247}, {"231", 255} // 16 + 5*36 + 5*6 + 5*1
    };

    for (size_t i=0; i < greyMain.size(); i++) {
        int val = std::get<1>(*(greyMain.begin()+i));
        termpaint_surface_write_with_colors(t.surface, i, 0, "x", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(val, val, val));
    }

    int column = 0;
    int row = 1;


    const std::initializer_list<std::tuple<int, std::string, int, std::string>> additionalTests = {
        {TERMPAINT_RGB_COLOR(255, 128, 128), "210", TERMPAINT_DEFAULT_COLOR, ""}, // 255,135,135 -> 16 + 5*36 + 2*6 + 2
        {TERMPAINT_RGB_COLOR(128, 255, 128), "120", TERMPAINT_DEFAULT_COLOR, ""}, // 135,255,135 -> 16 + 2*36 + 5*6 + 2
        {TERMPAINT_RGB_COLOR(128, 128, 255), "105", TERMPAINT_DEFAULT_COLOR, ""}, // 135,135,255 -> 16 + 2*36 + 2*6 + 5
        {TERMPAINT_DEFAULT_COLOR, "", TERMPAINT_RGB_COLOR(255, 128, 128), "210"}, // 255,135,135 -> 16 + 5*36 + 2*6 + 2
        {TERMPAINT_DEFAULT_COLOR, "", TERMPAINT_RGB_COLOR(128, 255, 128), "120"}, // 135,255,135 -> 16 + 2*36 + 5*6 + 2
        {TERMPAINT_DEFAULT_COLOR, "", TERMPAINT_RGB_COLOR(128, 128, 255), "105"}, // 135,135,255 -> 16 + 2*36 + 2*6 + 5
    };


    for (auto test : additionalTests) {
        termpaint_surface_write_with_colors(t.surface, column, row, "a", std::get<0>(test), std::get<2>(test));
        if (++column == 80) {
            column = 0;
            ++row;
        }
    }

    const auto grid = { 0, 95, 135, 175, 215, 255 };

    // exact palette grid values
    for (int r : grid) for (int g : grid) for (int b : grid) {
        termpaint_surface_write_with_colors(t.surface, column, row, "y", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(r, g, b));
        if (++column == 80) {
            column = 0;
            ++row;
        }
    }

    CHECK(row < 24);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    for (size_t i=0; i < greyMain.size(); i++) {
        std::string val = std::get<0>(*(greyMain.begin()+i));
        expected[{i, 0}] = singleWideChar("x").withBg(val);
    }

    column = 0;
    row = 1;


    for (auto test : additionalTests) {
        auto cell = singleWideChar("a");
        if (std::get<1>(test).size()) {
            cell = cell.withFg(std::get<1>(test));
        }
        if (std::get<3>(test).size()) {
            cell = cell.withBg(std::get<3>(test));
        }
        expected[{column, row}] = cell;
        if (++column == 80) {
            column = 0;
            ++row;
        }
    }

    // exact palette grid values
    for (size_t r_idx = 0; r_idx < grid.size(); r_idx++) for (size_t g_idx = 0; g_idx < grid.size(); g_idx++)
      for (size_t b_idx = 0; b_idx < grid.size(); b_idx++) {
        expected[{column, row}] = singleWideChar("y").withBg(std::to_string(16 + r_idx*36 + g_idx*6 + b_idx));
        if (++column == 80) {
            column = 0;
            ++row;
        }
    }

    checkEmptyPlusSome(s, expected);
}

TEST_CASE("rgb colors with quantize to 256 colors - grid bounds") {
    SimpleFullscreen t;
    termpaint_terminal_disable_capability(t.terminal, TERMPAINT_CAPABILITY_TRUECOLOR_MAYBE_SUPPORTED);
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    const auto grid = { 0, 95, 135, 175, 215, 255 };

    int column = 0;
    int row = 0;


    // (value, index)
    const std::initializer_list<std::tuple<int, int>>  gridBounds = {
        {0, 0}, {47, 0},
        {48, 1}, {114, 1},
        {115, 2}, {154, 2},
        {155, 3}, {194, 3},
        {195, 4}, {234, 4},
        {235, 5}, {255, 5}
    };

    const auto grey_ramp = {8, 18, 28, 38, 48, 58, 68, 78, 88, 98, 108, 118, 128, 138, 148, 158, 168, 178, 188, 198, 208, 218, 228, 238};

    // check bounds of boxes
    for (auto r_t : gridBounds) for (auto g_t : gridBounds) for (auto b_t : gridBounds) {
        int r_val = std::get<0>(r_t);
        int g_val = std::get<0>(g_t);
        int b_val = std::get<0>(b_t);

        termpaint_surface_write_with_colors(t.surface, column, row, "z", TERMPAINT_DEFAULT_COLOR, TERMPAINT_RGB_COLOR(r_val, g_val, b_val));
        if (++column == 80) {
            column = 0;
            ++row;
        }
    }

    CHECK(row < 24);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    column = 0;
    row = 0;

    // check bounds of boxes
    for (auto r_t : gridBounds) for (auto g_t : gridBounds) for (auto b_t : gridBounds) {
        int r_center = *(grid.begin() + std::get<1>(r_t));
        int r_idx = std::get<1>(r_t);
        int r_val = std::get<0>(r_t);
        int g_center = *(grid.begin() + std::get<1>(g_t));
        int g_idx = std::get<1>(g_t);
        int g_val = std::get<0>(g_t);
        int b_center = *(grid.begin() + std::get<1>(b_t));
        int b_idx = std::get<1>(b_t);
        int b_val = std::get<0>(b_t);

        std::string expected_color = std::to_string(16 + r_idx*36 + g_idx*6 + b_idx);

        auto sq = [](int x) { return x*x; };
        int best_metric = sq(r_center - r_val) + sq(g_center - g_val) + sq(b_center - b_val);
        for (size_t grey_index = 0; grey_index < grey_ramp.size(); grey_index++) {
            const int grey_quantized = *(grey_ramp.begin() + grey_index);
            const int cur_metric = sq(grey_quantized - r_val) + sq(grey_quantized - g_val) + sq(grey_quantized - b_val);
            if (cur_metric < best_metric) {
                expected_color = std::to_string(232 + grey_index);
                best_metric = cur_metric;
            }
        }

        expected[{column, row}] = singleWideChar("z").withBg(expected_color);
        if (++column == 80) {
            column = 0;
            ++row;
        }
    }

    checkEmptyPlusSome(s, expected);
}

TEST_CASE("named fg colors") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3,  3, " ", TERMPAINT_COLOR_BLACK, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 4,  3, " ", TERMPAINT_COLOR_RED, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 5,  3, " ", TERMPAINT_COLOR_GREEN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 6,  3, " ", TERMPAINT_COLOR_YELLOW, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 7,  3, " ", TERMPAINT_COLOR_BLUE, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 8,  3, " ", TERMPAINT_COLOR_MAGENTA, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 9,  3, " ", TERMPAINT_COLOR_CYAN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 10, 3, " ", TERMPAINT_COLOR_LIGHT_GREY, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3,  4, " ", TERMPAINT_COLOR_DARK_GREY, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 4,  4, " ", TERMPAINT_COLOR_BRIGHT_RED, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 5,  4, " ", TERMPAINT_COLOR_BRIGHT_GREEN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 6,  4, " ", TERMPAINT_COLOR_BRIGHT_YELLOW, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 7,  4, " ", TERMPAINT_COLOR_BRIGHT_BLUE, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 8,  4, " ", TERMPAINT_COLOR_BRIGHT_MAGENTA, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 9,  4, " ", TERMPAINT_COLOR_BRIGHT_CYAN, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 10, 4, " ", TERMPAINT_COLOR_WHITE, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, singleWideChar(" ").withFg("black")},
        {{ 3, 4 }, singleWideChar(" ").withFg("bright black")},
        {{ 4, 3 }, singleWideChar(" ").withFg("red")},
        {{ 4, 4 }, singleWideChar(" ").withFg("bright red")},
        {{ 5, 3 }, singleWideChar(" ").withFg("green")},
        {{ 5, 4 }, singleWideChar(" ").withFg("bright green")},
        {{ 6, 3 }, singleWideChar(" ").withFg("yellow")},
        {{ 6, 4 }, singleWideChar(" ").withFg("bright yellow")},
        {{ 7, 3 }, singleWideChar(" ").withFg("blue")},
        {{ 7, 4 }, singleWideChar(" ").withFg("bright blue")},
        {{ 8, 3 }, singleWideChar(" ").withFg("magenta")},
        {{ 8, 4 }, singleWideChar(" ").withFg("bright magenta")},
        {{ 9, 3 }, singleWideChar(" ").withFg("cyan")},
        {{ 9, 4 }, singleWideChar(" ").withFg("bright cyan")},
        {{ 10, 3 }, singleWideChar(" ").withFg("white")},
        {{ 10, 4 }, singleWideChar(" ").withFg("bright white")},
    });
}

TEST_CASE("named bg colors") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(t.surface, 3,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BLACK);
    termpaint_surface_write_with_colors(t.surface, 4,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_RED);
    termpaint_surface_write_with_colors(t.surface, 5,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_GREEN);
    termpaint_surface_write_with_colors(t.surface, 6,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_YELLOW);
    termpaint_surface_write_with_colors(t.surface, 7,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BLUE);
    termpaint_surface_write_with_colors(t.surface, 8,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_MAGENTA);
    termpaint_surface_write_with_colors(t.surface, 9,  3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_CYAN);
    termpaint_surface_write_with_colors(t.surface, 10, 3, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_LIGHT_GREY);
    termpaint_surface_write_with_colors(t.surface, 3,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_DARK_GREY);
    termpaint_surface_write_with_colors(t.surface, 4,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_write_with_colors(t.surface, 5,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_GREEN);
    termpaint_surface_write_with_colors(t.surface, 6,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_YELLOW);
    termpaint_surface_write_with_colors(t.surface, 7,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_BLUE);
    termpaint_surface_write_with_colors(t.surface, 8,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_MAGENTA);
    termpaint_surface_write_with_colors(t.surface, 9,  4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_CYAN);
    termpaint_surface_write_with_colors(t.surface, 10, 4, " ", TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_WHITE);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 3, 3 }, singleWideChar(" ").withBg("black")},
        {{ 3, 4 }, singleWideChar(" ").withBg("bright black")},
        {{ 4, 3 }, singleWideChar(" ").withBg("red")},
        {{ 4, 4 }, singleWideChar(" ").withBg("bright red")},
        {{ 5, 3 }, singleWideChar(" ").withBg("green")},
        {{ 5, 4 }, singleWideChar(" ").withBg("bright green")},
        {{ 6, 3 }, singleWideChar(" ").withBg("yellow")},
        {{ 6, 4 }, singleWideChar(" ").withBg("bright yellow")},
        {{ 7, 3 }, singleWideChar(" ").withBg("blue")},
        {{ 7, 4 }, singleWideChar(" ").withBg("bright blue")},
        {{ 8, 3 }, singleWideChar(" ").withBg("magenta")},
        {{ 8, 4 }, singleWideChar(" ").withBg("bright magenta")},
        {{ 9, 3 }, singleWideChar(" ").withBg("cyan")},
        {{ 9, 4 }, singleWideChar(" ").withBg("bright cyan")},
        {{ 10, 3 }, singleWideChar(" ").withBg("white")},
        {{ 10, 4 }, singleWideChar(" ").withBg("bright white")},
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
        {{ 3, 3 }, singleWideChar(" ").withBg("16")},
        {{ 3, 4 }, singleWideChar(" ").withFg("16")},
        {{ 4, 3 }, singleWideChar(" ").withBg("51")},
        {{ 4, 4 }, singleWideChar(" ").withFg("51")},
        {{ 5, 3 }, singleWideChar(" ").withBg("70")},
        {{ 5, 4 }, singleWideChar(" ").withFg("70")},
        {{ 6, 3 }, singleWideChar(" ").withBg("110")},
        {{ 6, 4 }, singleWideChar(" ").withFg("110")},
        {{ 7, 3 }, singleWideChar(" ").withBg("123")},
        {{ 7, 4 }, singleWideChar(" ").withFg("123")},
        {{ 8, 3 }, singleWideChar(" ").withBg("213")},
        {{ 8, 4 }, singleWideChar(" ").withFg("213")},
        {{ 9, 3 }, singleWideChar(" ").withBg("232")},
        {{ 9, 4 }, singleWideChar(" ").withFg("232")},
        {{ 10, 3 }, singleWideChar(" ").withBg("255")},
        {{ 10, 4 }, singleWideChar(" ").withFg("255")},
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
        {{ 3, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_BOLD)},
        {{ 4, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_ITALIC)},
        {{ 5, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_BLINK)},
        {{ 6, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_INVERSE)},
        {{ 7, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_STRIKE)},
        {{ 8, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE)},
        {{ 9, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_DBL)},
        {{ 10, 3 }, singleWideChar("X").withStyle(TERMPAINT_STYLE_UNDERLINE_CURLY)},
    });
}


TEST_CASE("cleared but colored") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_clear_rect(t.surface, 5, 2, 2, 2, TERMPAINT_COLOR_RED, TERMPAINT_COLOR_BLUE);
    termpaint_surface_clear_rect(t.surface, 8, 2, 2, 2, TERMPAINT_COLOR_CYAN, TERMPAINT_COLOR_WHITE);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    checkEmptyPlusSome(s, {
        {{ 5, 2 }, singleWideChar(" ").setErased().withFg("red").withBg("blue")},
        {{ 6, 2 }, singleWideChar(" ").setErased().withFg("red").withBg("blue")},
        {{ 5, 3 }, singleWideChar(" ").setErased().withFg("red").withBg("blue")},
        {{ 6, 3 }, singleWideChar(" ").setErased().withFg("red").withBg("blue")},
        {{ 8, 2 }, singleWideChar(" ").setErased().withFg("cyan").withBg("bright white")},
        {{ 9, 2 }, singleWideChar(" ").setErased().withFg("cyan").withBg("bright white")},
        {{ 8, 3 }, singleWideChar(" ").setErased().withFg("cyan").withBg("bright white")},
        {{ 9, 3 }, singleWideChar(" ").setErased().withFg("cyan").withBg("bright white")},
    });
}


TEST_CASE("wrapped line") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    std::string str1 = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor i";
    termpaint_surface_write_with_colors(t.surface, 0,  4, str1.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    std::string str2 = "ncididunt ut labore et dolore magna";
    termpaint_surface_write_with_colors(t.surface, 0,  5, str2.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_set_softwrap_marker(t.surface, 79, 4, true);
    termpaint_surface_set_softwrap_marker(t.surface, 0, 5, true);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    for (size_t i = 0; i < str1.size(); i++) {
        expected[{ i, 4 }] = singleWideChar(str1.substr(i, 1)).withBg("bright red");
    }

    for (size_t i = 0; i < str2.size(); i++) {
        expected[{ i, 5 }] = singleWideChar(str2.substr(i, 1)).withBg("bright red");
    }


    checkEmptyPlusSome(s, expected, Overrides().withSoftWrappedLines({4}));
}


TEST_CASE("wrapped line - missing initial marker") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    std::string str1 = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor i";
    termpaint_surface_write_with_colors(t.surface, 0,  4, str1.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    std::string str2 = "ncididunt ut labore et dolore magna";
    termpaint_surface_write_with_colors(t.surface, 0,  5, str2.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_set_softwrap_marker(t.surface, 0, 5, true);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    for (size_t i = 0; i < str1.size(); i++) {
        expected[{ i, 4 }] = singleWideChar(str1.substr(i, 1)).withBg("bright red");
    }

    for (size_t i = 0; i < str2.size(); i++) {
        expected[{ i, 5 }] = singleWideChar(str2.substr(i, 1)).withBg("bright red");
    }


    checkEmptyPlusSome(s, expected, Overrides().withSoftWrappedLines({}));
}


TEST_CASE("wrapped line - missing continuation marker") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    std::string str1 = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor i";
    termpaint_surface_write_with_colors(t.surface, 0,  4, str1.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    std::string str2 = "ncididunt ut labore et dolore magna";
    termpaint_surface_write_with_colors(t.surface, 0,  5, str2.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_set_softwrap_marker(t.surface, 79, 4, true);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    for (size_t i = 0; i < str1.size(); i++) {
        expected[{ i, 4 }] = singleWideChar(str1.substr(i, 1)).withBg("bright red");
    }

    for (size_t i = 0; i < str2.size(); i++) {
        expected[{ i, 5 }] = singleWideChar(str2.substr(i, 1)).withBg("bright red");
    }


    checkEmptyPlusSome(s, expected, Overrides().withSoftWrappedLines({}));
}


TEST_CASE("wrapped line - misplaced initial marker") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    std::string str1 = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor i";
    termpaint_surface_write_with_colors(t.surface, 0,  4, str1.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    std::string str2 = "ncididunt ut labore et dolore magna";
    termpaint_surface_write_with_colors(t.surface, 0,  5, str2.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_set_softwrap_marker(t.surface, 78, 4, true);
    termpaint_surface_set_softwrap_marker(t.surface, 0, 5, true);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    for (size_t i = 0; i < str1.size(); i++) {
        expected[{ i, 4 }] = singleWideChar(str1.substr(i, 1)).withBg("bright red");
    }

    for (size_t i = 0; i < str2.size(); i++) {
        expected[{ i, 5 }] = singleWideChar(str2.substr(i, 1)).withBg("bright red");
    }


    checkEmptyPlusSome(s, expected, Overrides().withSoftWrappedLines({}));
}


TEST_CASE("wrapped line - misplaced continuation marker") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    std::string str1 = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor i";
    termpaint_surface_write_with_colors(t.surface, 0,  4, str1.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    std::string str2 = "ncididunt ut labore et dolore magna";
    termpaint_surface_write_with_colors(t.surface, 0,  5, str2.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_set_softwrap_marker(t.surface, 79, 4, true);
    termpaint_surface_set_softwrap_marker(t.surface, 1, 5, true);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    for (size_t i = 0; i < str1.size(); i++) {
        expected[{ i, 4 }] = singleWideChar(str1.substr(i, 1)).withBg("bright red");
    }

    for (size_t i = 0; i < str2.size(); i++) {
        expected[{ i, 5 }] = singleWideChar(str2.substr(i, 1)).withBg("bright red");
    }


    checkEmptyPlusSome(s, expected, Overrides().withSoftWrappedLines({}));
}


TEST_CASE("wrapped line - wide wrapping character") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    std::string str1 = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod temporあ";
    termpaint_surface_write_with_colors(t.surface, 0,  4, str1.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    std::string str2 = "ncididunt ut labore et dolore magna";
    termpaint_surface_write_with_colors(t.surface, 0,  5, str2.data(), TERMPAINT_DEFAULT_COLOR, TERMPAINT_COLOR_BRIGHT_RED);
    termpaint_surface_set_softwrap_marker(t.surface, 78, 4, true);
    termpaint_surface_set_softwrap_marker(t.surface, 0, 5, true);

    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();

    std::map<std::tuple<int,int>, CapturedCell> expected;

    for (size_t i = 0; i < str1.size() - 1; i++) {
        expected[{ i, 4 }] = singleWideChar(str1.substr(i, 1)).withBg("bright red");
    }
    expected[{ 78, 4 }] = doubleWideChar("あ").withBg("bright red");

    for (size_t i = 0; i < str2.size(); i++) {
        expected[{ i, 5 }] = singleWideChar(str2.substr(i, 1)).withBg("bright red");
    }


    checkEmptyPlusSome(s, expected, Overrides().withSoftWrappedLines({}));
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


TEST_CASE("pause with mouse") {
    SimpleFullscreen t;
    termpaint_surface_clear(t.surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_terminal_set_mouse_mode(t.terminal, TERMPAINT_MOUSE_MODE_MOVEMENT);
    termpaint_terminal_flush(t.terminal, false);

    CapturedState s = capture();
    checkEmptyPlusSome(s, {});
    CHECK(s.mouseMode == "movement");

    termpaint_terminal_pause(t.terminal);

    s = capture();
    CHECK(s.mouseMode == "");

    termpaint_terminal_unpause(t.terminal);
    termpaint_terminal_flush(t.terminal, false);

    s = capture();
    checkEmptyPlusSome(s, {});

    CHECK(s.mouseMode == "movement");
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

