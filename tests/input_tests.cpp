#include <string.h>
#include <functional>
#include <fstream>

#include "../third-party/catch.hpp"
#include "../third-party/picojson.h"

#if __GNUC__
#pragma GCC diagnostic ignored "-Wpadded"
#endif

typedef bool _Bool;

using jarray = picojson::value::array;
using jobject = picojson::value::object;

#include "../termpaint_input.h"

template <typename Result, typename... Args>
Result wrapper(void* state, Args... args) {
    using FnType = std::function<Result(Args...)>;
    FnType& w = *reinterpret_cast<FnType*>(state);
    return w(args...);
}

template <typename TYPE1, typename Result, typename... Args>
void wrap(void (*set_cb)(TYPE1 *ctx, Result (*cb)(void *user_data, Args...), void *user_data), TYPE1 *ctx, std::function<Result(Args...)> &fn) {
    void *state = reinterpret_cast<void*>(&fn);
    set_cb(ctx, &wrapper<Result, Args...>, state);
}


bool parses_as_one(const char *input) {
    enum { START, OK, ERROR } state = START;
    std::function<_Bool(const char*, unsigned, _Bool overflow)> callback = [input, &state] (const char *data, unsigned length, _Bool overflow) -> _Bool {
        (void)overflow;
        if (state == START) {
            if (length == strlen(input) && memcmp(input, data, length) == 0) {
                state = OK;
            } else {
                state = ERROR;
            }
        } else if (state == OK) {
            state = ERROR;
        }
        return true;
    };

    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_raw_filter_cb, input_ctx, callback);
    termpaint_input_add_data(input_ctx, input, strlen(input));
    termpaint_input_free(input_ctx);

    return state == OK;
}

#define SS3_7 "\eO"
#define SS3_8 "\x8f"

#define CSI7 "\e["
#define CSI8 "\x9b"

#define DCS7 "\eP"
#define DCS8 "\x90"

#define OSC7 "\e]"
#define OSC8 "\x9d"

#define ST7 "\e\\"
#define ST8 "\x9c"

TEST_CASE( "Input is correctly seperated", "[sep]" ) {
    REQUIRE(parses_as_one("A"));
    REQUIRE(parses_as_one("a"));
    REQUIRE(parses_as_one(CSI7 "1;3A")); // xterm: alt-arrow_up
    REQUIRE(parses_as_one(CSI8 "1;3A")); // xterm: alt-arrow_up with 8bit CSI
    REQUIRE(parses_as_one(DCS7 "1$r0m" ST7)); // possible reply to "\eP$qm\e\\";
    REQUIRE(parses_as_one(DCS8 "1$r0m" ST8));
    REQUIRE(!parses_as_one(DCS8 "1$r0m\007"));
    REQUIRE(parses_as_one(OSC7 "lsome title" ST7)); // possible reply to "\[21t"
    REQUIRE(parses_as_one(OSC7 "lsome title\007"));
    REQUIRE(parses_as_one(SS3_7 "P")); // F1
    REQUIRE(parses_as_one(SS3_8 "P")); // F1
}

TEST_CASE("input: raw filter") {
    struct TestCase { const std::string sequence; int events; };
    const auto testCase = GENERATE(
        TestCase{ "\033[1;1$y", 1 },
        TestCase{ "\033[1;1R", 1 },
        TestCase{ "\033\033a", 2 },
        TestCase{ "\033\033[1;1R", 2 }
    );
    bool do_filter_out = GENERATE( false, true );
    INFO((testCase.sequence[1] == '\033' ? "ESC ESC" + testCase.sequence.substr(2) : "ESC" + testCase.sequence.substr(1)));

    CAPTURE(do_filter_out);

    std::string string_in_filter;

    std::function<_Bool(const char*, unsigned, _Bool overflow)> callback
            = [&] (const char *data, unsigned length, _Bool overflow) -> _Bool {
        string_in_filter += std::string(data, length);
        REQUIRE(!overflow);
        return do_filter_out;
    };

    int no_events = 0;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        (void)event;
        if (do_filter_out) {
            FAIL("should have been filtered out");
        } else {
            ++no_events;
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    wrap(termpaint_input_set_raw_filter_cb, input_ctx, callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    if (!do_filter_out) {
        REQUIRE(no_events == testCase.events);
    }
    REQUIRE(string_in_filter == testCase.sequence);
    termpaint_input_free(input_ctx);
}

TEST_CASE( "evil utf8" ) {
    std::vector<termpaint_event> events;
    unsigned num_events = 0;

    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (num_events < events.size()) {
            INFO("event index " << num_events);
            auto& expected = events[num_events];
            REQUIRE(expected.type == event->type);
            REQUIRE(expected.c.length == event->c.length);
            REQUIRE(memcmp(expected.c.string, event->c.string, expected.c.length) == 0);
        } else {
            FAIL("more events than expected");
        }
        ++num_events;
    };

    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    std::string input;

    SECTION("X") {
        input = "\x41\xc2\x3e";
        events.resize(3);
        events[0].type = TERMPAINT_EV_CHAR;
        events[0].c.length = 1;
        events[0].c.string = "\x41";
        events[1].type = TERMPAINT_EV_INVALID_UTF8;
        events[1].c.length = 1;
        events[1].c.string = "\xc2";
        events[2].type = TERMPAINT_EV_CHAR;
        events[2].c.length = 1;
        events[2].c.string = "\x3e";

        termpaint_input_add_data(input_ctx, input.data(), input.size());
        REQUIRE(num_events == events.size());
    }
    termpaint_input_free(input_ctx);
}

// TODO more malformed utf8 tests?
TEST_CASE("input: malformed utf8") {
    struct TestCase { const std::string sequence; const std::string desc; };
    const auto testCase = GENERATE(
        TestCase{ "\xff",                 "0xff" },
        TestCase{ "\xfe",                 "0xfe" },
        //TestCase{ "\x80",                 "lone continuation byte" }, for now this is tested for in malformed sequences
        TestCase{ "\xc0\xa0",             "overlong (2) encoding of 32" },
        TestCase{ "\xc1\xa0",             "overlong (2) encoding of 96" },
        TestCase{ "\xe0\x80\xa0",         "overlong (3) encoding of 32" },
        TestCase{ "\xf0\x80\x80\xa0",     "overlong (4) encoding of 32" },
        TestCase{ "\033\xff",             "ESC + 0xff" },
        TestCase{ "\033\xfe",             "ESC + 0xfe" },
        TestCase{ "\033\xc0\xa0",         "ESC + overlong (2) encoding of 32" },
        TestCase{ "\033\xc1\xa0",         "ESC + overlong (2) encoding of 96" },
        TestCase{ "\033\xe0\x80\xa0",     "ESC + overlong (3) encoding of 32" },
        TestCase{ "\033\xf0\x80\x80\xa0", "ESC + overlong (4) encoding of 32" },

        TestCase{ "\xf8\xbf\xbf\xbf\xbf",     "5 byte utf-8 like encoding" },
        TestCase{ "\xfc\xbf\xbf\xbf\xbf\xbf", "6 byte utf-8 like encoding" },
        TestCase{ "\033\xf8\xbf\xbf\xbf\xbf",     "ESC + 5 byte utf-8 like encoding" },
        TestCase{ "\033\xfc\xbf\xbf\xbf\xbf\xbf", "ESC + 6 byte utf-8 like encoding" },

        TestCase{ "\xfcz",                "6 byte start byte followed by non continuation" },
        TestCase{ "\xf8z",                "5 byte start byte followed by non continuation" },
        TestCase{ "\xf0z",                "4 byte start byte followed by non continuation" },
        TestCase{ "\xe0z",                "3 byte start byte followed by non continuation" },
        TestCase{ "\xc2z",                "2 byte start byte followed by non continuation" }

    );
    INFO(testCase.desc);
    enum { START, GOT_EVENT } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            if (testCase.sequence[testCase.sequence.size()-1] != 'z') {
                FAIL("more events than expected");
            }
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_INVALID_UTF8);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    termpaint_input_free(input_ctx);
}

TEST_CASE( "Overflow is handled correctly", "[overflow]" ) {
    enum { START, GOT_RAW, GOT_EVENT, ERROR } state = START;
    std::function<_Bool(const char*, unsigned, _Bool overflow)> raw_callback
            = [&state] (const char *data, unsigned length, _Bool overflow) -> _Bool {
        (void)data; (void)length;
        if (state == START) {
            if (overflow) {
                state = GOT_RAW;
            } else {
                state = ERROR;
            }
        } else {
            state = ERROR;
        }
        return false;
    };

    termpaint_event captured_event;

    std::function<void(termpaint_event* event)> event_callback
            = [&state, &captured_event] (termpaint_event* event) -> void {

        if (state == GOT_RAW) {
            captured_event = *event;
            state = GOT_EVENT;
        } else {
            state = ERROR;
        }
    };

    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_raw_filter_cb, input_ctx, raw_callback);
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    std::string input;

    // Workaround for Catch #734
    auto runTest = [&] {
        termpaint_input_add_data(input_ctx, input.data(), input.size());

        REQUIRE(state == GOT_EVENT);
        REQUIRE(captured_event.type == TERMPAINT_EV_OVERFLOW);
        /*REQUIRE(captured_event.length == 0);
        REQUIRE(captured_event.atom_or_string == nullptr);
        REQUIRE(captured_event.modifier == 0);*/
    };

    SECTION( "CSI7" ) {
        input = CSI7 + std::string(2000, '1') + "A";
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }

    SECTION( "CSI8" ) {
        input = CSI8 + std::string(2000, '1') + "A";
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }

    SECTION( "SS3_7" ) {
        input = SS3_7 + std::string(2000, '1') + "A";
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }

    SECTION( "SS3_8" ) {
        input = SS3_8 + std::string(2000, '1') + "A";
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }

    SECTION( "DCS7" ) {
        input = DCS7 + std::string(2000, '1') + "A" ST7;
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }
    SECTION( "DCS8" ) {
        input = DCS8 + std::string(2000, '1') + "A" ST8;
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }
    SECTION( "OSC7" ) {
        input = OSC7 + std::string(2000, '1') + "A" ST7;
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }
    SECTION( "OSC8" ) {
        input = OSC8 + std::string(2000, '1') + "A" ST8;
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    }
    SECTION( "CSI retrigger" ) {
        input = std::string("\033[") + std::string(1022, '.') + "\033[";
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 2);
    }
    SECTION( "OSC retrigger" ) {
        input = std::string("\033]") + std::string(1022, '.') + "\033[";
        runTest();
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 2);
    }
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: double esc handling") {
    struct TestCase { const std::string sequence; int type; const std::string desc; };
    const auto testCase = GENERATE(
        TestCase{ "\033\033[  @",                TERMPAINT_EV_UNKNOWN,         "short unrecognised sequence" },
        TestCase{ "\033\033[                 @", TERMPAINT_EV_UNKNOWN,         "long unrecognised sequence" },
        TestCase{ "\033\033[10;21R",             TERMPAINT_EV_CURSOR_POSITION, "cursor position report" }
    );
    enum { START, GOT_ESC, GOT_EVENT } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            CHECK(event->type == TERMPAINT_EV_KEY);
            CHECK(event->key.modifier == 0);
            CHECK(event->key.atom == termpaint_input_escape());
            CHECK(event->key.length == strlen(termpaint_input_escape()));
            state = GOT_ESC;
        } else if (state == GOT_ESC) {
            REQUIRE(event->type == testCase.type);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: unmapped/rejected sequences") {
    struct TestCase { const std::string sequence; const std::string desc; int tweak = 0; };
    const auto testCase = GENERATE(
        TestCase{ "\033[0[", "CSI with parameter and [ as final" },

        TestCase{ "\033[100R",      "cursor position report, missing second parameter" },
        TestCase{ "\033[1R",        "cursor position report, to short by missing second parameter" },
        TestCase{ "\033[1:0R",      "cursor position report, subparameter 1" },
        TestCase{ "\033[1;1:0R",    "cursor position report, subparameter 2" },
        TestCase{ "\033[1;0R",      "cursor position report, zero 1st parameter" },
        TestCase{ "\033[0;10R",     "cursor position report, zero 2nd parameter" },
        TestCase{ "\033[1;2147483648R", "cursor position report, out of int range 1" },
        TestCase{ "\033[1;2147483650R", "cursor position report, out of int range 2" },
        TestCase{ "\033[1\x01;1R", "cursor position report, embedded C0" },
        TestCase{ "\033[1\x80;1R", "cursor position report, embedded 0x80" },

        TestCase{ "\033[100$y",     "mode report, missing second parameter" },
        TestCase{ "\033[1$y",       "mode report, to short by missing second parameter" },
        TestCase{ "\033[1:0$y",     "mode report, subparameter 1" },
        TestCase{ "\033[1;1:0$y",   "mode report, subparameter 2" },

        TestCase{ "\033[10000M",    "mouse report, missing second and third parameter" },
        TestCase{ "\033[10;10M",    "mouse report, missing third parameter" },
        TestCase{ "\033[1M",        "mouse report, to short by missing second and third parameter" },
        TestCase{ "\033[10:00M",    "mouse report, subparameter 1" },
        TestCase{ "\033[1;1:0M",    "mouse report, subparameter 2" },
        TestCase{ "\033[1;1;1:0M",  "mouse report, subparameter 3" },
        TestCase{ "\033[1;1;1;0M",  "mouse report, excess parameters" },
        TestCase{ "\033[0;33;33M",    "mouse report, zero 1st parameter" },
        TestCase{ "\033[33;0;33M",    "mouse report, zero 2nd parameter" },
        TestCase{ "\033[33;33;0M",    "mouse report, zero 3rd parameter" },
        TestCase{ "\033[1;2147483648;1M", "mouse report, excess value" },

        TestCase{ "\033[<10000M",    "mouse report, missing second and third parameter" },
        TestCase{ "\033[<10;10M",    "mouse report, missing third parameter" },
        TestCase{ "\033[<1M",        "mouse report, to short by missing second and third parameter" },
        TestCase{ "\033[<10:00M",    "mouse report, subparameter 1" },
        TestCase{ "\033[<1;1:0M",    "mouse report, subparameter 2" },
        TestCase{ "\033[<1;1;1:0M",  "mouse report, subparameter 3" },
        TestCase{ "\033[<1;1;1;0M",  "mouse report, excess parameters" },
        TestCase{ "\033[<1;0;1M",    "mouse report, zero 2nd parameter" },
        TestCase{ "\033[<1;1;0M",    "mouse report, zero 3rd parameter" },
        TestCase{ "\033[<1;2147483648;1M", "mouse report, excess value" },

        TestCase{ {"\033[M \000!", 6},"legacy mouse report, overflow x parameter" },
        TestCase{ {"\033[M !\000", 6},"legacy mouse report, overflow y parameter" },
        TestCase{ {"\033[M\000!!", 6},"legacy mouse report, zero button parameter" },

        TestCase{ {"\033[M \001!", 6},"legacy mouse report, uncontrolled (1) overflow x parameter" },
        TestCase{ {"\033[M !\001", 6},"legacy mouse report, uncontrolled (1) overflow y parameter" },

        TestCase{ {"\033[M \037!", 6},"legacy mouse report, uncontrolled (31) overflow x parameter" },
        TestCase{ {"\033[M !\037", 6},"legacy mouse report, uncontrolled (31) overflow y parameter" },

        TestCase{ "\033[M\xc0\xc0!!","legacy multi-byte mouse report, invalid btn multi byte encoding", 1 },
        TestCase{ "\033[M \xc0\xc0!","legacy multi-byte mouse report, invalid x multi byte encoding", 1 },
        TestCase{ "\033[M !\xc0\xc0","legacy multi-byte mouse report, invalid y multi byte encoding", 1 },
        TestCase{ "\033[M!!\xe0\xc0","legacy multi-byte mouse report, invalid y multi byte encoding 2", 1 },
        TestCase{ "\033[M!\xe0\xc0!","legacy multi-byte mouse report, invalid x multi byte encoding 2", 1 },
        TestCase{ "\033[M\xf0\xc0!!","legacy multi-byte mouse report, invalid btn multi byte encoding 2", 1 },
        TestCase{ "\033[M\xf8\x80\x80\x80\x80\xc0!!","legacy multi-byte mouse report, overlong btn multi byte encoding 5 bytes", 1 },
        TestCase{ "\033[M\x80\xc4\xa8!","legacy mouse multi-byte report, invalid btn multi byte only continuations", 1 },

        TestCase{ "\033[M \xc4\xa8\x01","legacy mouse multi-byte report, uncontrolled (1) overflow y parameter", 1 },
        TestCase{ "\033[M \x01\xc4\xa8","legacy mouse multi-byte report, uncontrolled (1) overflow x parameter", 1 },
        TestCase{ "\033[M\x01!\xc4\xa8","legacy mouse multi-byte report, button parameter out of range", 1 },

        TestCase{ "\x9b M", "8 bit CSI with space and M" },

        TestCase{ "\033[27;10;1@",  "CSI @ with start like modify other" },
        TestCase{ "\033[27;10;~",   "modify other without character number" },
        TestCase{ "\033[27; 00~",   "modify other with space before the parameter" },
        TestCase{ "\033[27;?00~",   "modify other with question mark before the parameter" },
        TestCase{ "\033[27;1; ~",   "modify other with space after the parameter" },
        TestCase{ "\033[27;0;1~",   "modify other with mod == 0" },
        TestCase{ "\033[27;2147483648;1~", "modify other with out of range parameter" },

        TestCase{ "\033]\033\\",    "empty OSC sequence" },
        TestCase{ "\033]\007",      "empty OSC sequence (BEL)" },
        TestCase{ "\033]XX\033\\",  "non numeric OSC sequence (X)" },
        TestCase{ "\033]XX\007",    "non numeric OSC sequence (X,BEL)" },
        TestCase{ "\033]  \033\\",  "non numeric OSC sequence (space)" },
        TestCase{ "\033]  \007",    "non numeric OSC sequence (space,BEL)" },
        TestCase{ "\033]00\033\\",  "numeric OSC sequence without additional payload" },
        TestCase{ "\033]00\007",    "numeric OSC sequence without additional payload (BEL)" },
        TestCase{ "\033]0X\033\\",  "OSC sequence with inital digit but non digit following (X)" },
        TestCase{ "\033]0X\007",    "OSC sequence with inital digit but non digit following (X,BEL)" },
        TestCase{ "\033]0*\033\\",  "OSC sequence with inital digit but non digit following (*)" },
        TestCase{ "\033]0*\007",    "OSC sequence with inital digit but non digit following (*,BEL)" },

        TestCase{ "\x80",            "0x80" },
        TestCase{ "\033\x80",       "ESC + 0x80" },
        TestCase{ "\033[?\x1f@",    "CSI with embedded control character" },
        TestCase{ "\033]abc\\\033x", "OSC with backslash and mistermination" },

        TestCase{ "\033]0;\033\\", "OSC sequence with unassigned number 0" },
        TestCase{ "\033]0;\007",   "OSC sequence with unassigned number 0 (BEL)" },
        TestCase{ "\033]700;\033\\", "OSC sequence with unassigned number" },
        TestCase{ "\033]700;\007",   "OSC sequence with unassigned number (BEL)" },
        TestCase{ "\033]999999;\033\\", "OSC sequence with huge number (no overflowing)" },
        TestCase{ "\033]999999;\007",   "OSC sequence with huge number (no overflowing,BEL)" },
        TestCase{ "\033]2147483648\033\\", "OSC sequence with huge number (outside int range1)" },
        TestCase{ "\033]2147483648\007",   "OSC sequence with huge number (outside int range1,BEL)" },
        TestCase{ "\033]2147483650\033\\", "OSC sequence with huge number (outside int range2)" },
        TestCase{ "\033]2147483650\007",   "OSC sequence with huge number (outside int range2,BEL)" },

        TestCase{ "\033]4;2147483648;rgb:0000/0000/0000\033\\", "OSC 4 sequence with huge number (outside int range)" },
        TestCase{ "\033]4;2147483648;rgb:0000/0000/0000\007",   "OSC 4 sequence with huge number (outside int range,BEL)" },

        TestCase{ "\033]700;\033[  @", "unterminated OSC sequence", 2 },

        TestCase{ "\033P700\033\\",  "DCS with 700"},
        TestCase{ "\033P700\007",    "DCS with 700 (BEL)"},
        TestCase{ "\033P!700\033\\", "DCS with !700" },
        TestCase{ "\033P!700\007",   "DCS with !700 (BEL)" },
        TestCase{ "\033Pabc\\\033x", "DSC with backslash and mistermination" },

        TestCase{ "\033O;Z", "SS3 with empty parameters" }
    );
    INFO(testCase.desc);
    enum { START, GOT_EVENT, GOT_EVENT2 } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            if (testCase.tweak & 2) {
                state = GOT_EVENT2;
            } else {
                FAIL("more events than expected");
            }
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_UNKNOWN);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    termpaint_input_expect_legacy_mouse_reports(input_ctx, (testCase.tweak & 1) ? TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE_MODE_1005 : TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE);
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    if (testCase.tweak & 2) {
        REQUIRE(state == GOT_EVENT2);
    } else {
        REQUIRE(state == GOT_EVENT);
    }
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: nullptr as handler") {
    struct TestCase { const std::string sequence; const std::string desc; };
    const auto testCase = GENERATE(
        TestCase{ "\033[1;1R", "1" },
        TestCase{ "\033\033\033[6n", "2" },
        TestCase{ "\033\033[1;1R", "3" }
    );
    INFO(testCase.desc);
    // This mostly tests that this is not crashing.
    termpaint_input *input_ctx = termpaint_input_new();
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

static int hexToInt(char input) {
    if ('0' <= input && input <= '9') {
        return input - '0';
    } else if ('a' <= input && input <= 'f') {
        return input - 'a' + 10;
    } else if ('A' <= input && input <= 'F') {
        return input - 'A' + 10;
    } else {
        FAIL("fixture file is broken");
    }
    return -1;
}

TEST_CASE( "Recorded sequences parsed as usual", "[pin-recorded]" ) {
    std::ifstream istrm("../tests/input_tests.json", std::ios::binary);
    picojson::value rootval;
    istrm >> rootval;
    if (istrm.fail()) {
        FAIL("Error while reading input_tests.json:" << picojson::get_last_error());
    }
    jarray cases = rootval.get<jarray>();

    for (auto caseval: cases) {
        jobject caseobj = caseval.get<jobject>();
        std::string rawInputHex = caseobj["raw"].get<std::string>();
        std::string sectionName = caseobj["keyId"].get<std::string>() + "-" + rawInputHex;

        CAPTURE(sectionName);
        CAPTURE(rawInputHex);
        std::string rawInput;
        for (int i=0; i < rawInputHex.size(); i+=2) {
            unsigned char ch;
            ch = (hexToInt(rawInputHex[i]) << 4) + hexToInt(rawInputHex[i+1]);
            rawInput.push_back(static_cast<char>(ch));
        }

        std::string expectedType = caseobj["type"].get<std::string>();
        std::string expectedValue;
        if (expectedType == "key") {
            expectedValue = caseobj["key"].get<std::string>();
        } else if (expectedType == "char") {
            expectedValue = caseobj["chars"].get<std::string>();
        } else {
            FAIL("Type in fixture not right: " + expectedType);
        }
        std::string expectedModStr = caseobj["mod"].get<std::string>();
        CAPTURE(expectedType);
        CAPTURE(expectedModStr);
        CAPTURE(expectedValue);

        enum { START, GOT_EVENT, GOT_SYNC } state = START;
        bool expectSync = false;

        std::function<void(termpaint_event* event)> event_callback
                = [&] (termpaint_event* event) -> void {
            if (state == GOT_EVENT && !expectSync) {
                FAIL("more events than expected");
            } else if (state == START) {
                std::string actualType;
                std::string actualValue;
                int actualModifier = 0;
                if (event->type == TERMPAINT_EV_CHAR) {
                    actualType = "char";
                    actualValue = std::string(event->c.string, event->c.length);
                    actualModifier = event->c.modifier;
                } else if (event->type == TERMPAINT_EV_KEY) {
                    actualType = "key";
                    actualValue = std::string(event->key.atom);
                    actualModifier = event->key.modifier;
                } else {
                    actualType = "???";
                }
                int expectedMod = 0;
                if (expectedModStr.find('S') != std::string::npos) {
                    expectedMod |= TERMPAINT_MOD_SHIFT;
                }
                if (expectedModStr.find('A') != std::string::npos) {
                    expectedMod |= TERMPAINT_MOD_ALT;
                }
                if (expectedModStr.find('C') != std::string::npos) {
                    expectedMod |= TERMPAINT_MOD_CTRL;
                }
                CAPTURE(actualValue);
                REQUIRE(expectedType == actualType);
                if (event->type == TERMPAINT_EV_CHAR) {
                    REQUIRE(expectedValue.size() == event->c.length);
                }
                REQUIRE(expectedValue == actualValue);
                REQUIRE(expectedMod == actualModifier);
                state = GOT_EVENT;
            } else if (state == GOT_EVENT) {
                bool wasSync = event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync();
                REQUIRE(wasSync);
                state = GOT_SYNC;
            } else {
                FAIL("unexpected state " << state);
            }
        };

        if (rawInputHex == "1b" // ESC: the traditional hard case
                || rawInputHex == "1b1b" // alt ESC
                || rawInputHex == "1b50"
                || rawInputHex == "1b4f"
                // various urxvt sequences end in '$', which is not a final character for CSI
                || rawInputHex == "1b5b323324" // urxvt F11.S
                || rawInputHex == "1b1b5b323324" // urxvt F11.SA
                || rawInputHex == "1b5b323424" // urxvt F12.S
                || rawInputHex == "1b1b5b323424" // urxvt F12.SA
                || rawInputHex == "1b5b3224" // urxvt Insert.S
                || rawInputHex == "1b1b5b3224" // urxvt Insert.SA
                || rawInputHex == "1b5b3324" // urxvt Delete.S
                || rawInputHex == "1b1b5b3324" // urxvt Delete.SA
                || rawInputHex == "1b5b3524" // urxvt PageUp.S
                || rawInputHex == "1b1b5b3524" // urxvt PageUp.SA
                || rawInputHex == "1b5b3624" // urxvt PageDown.S
                || rawInputHex == "1b1b5b3624" // urxvt PageDown.SA
                || rawInputHex == "1b5b3724" // urxvt Home.S
                || rawInputHex == "1b1b5b3724" // urxvt Home.SA
                || rawInputHex == "1b5b3824" // urxvt End.S
                || rawInputHex == "1b1b5b3824" // urxvt End.SA
                ) {
            expectSync = true;
        }

        termpaint_input *input_ctx = termpaint_input_new();
        wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
        std::string input;
        termpaint_input_add_data(input_ctx, rawInput.data(), rawInput.size());
        if (!expectSync) {
            REQUIRE(state == GOT_EVENT);
        } else {
            REQUIRE(state == START);
            termpaint_input_add_data(input_ctx, "\e[0n", 4);
            REQUIRE(state == GOT_SYNC);
        }
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
        termpaint_input_free(input_ctx);
    }
}

TEST_CASE("input: quirk: backspace 0x08/0x7f swapped") {
    struct TestCase { std::string rawInput; int mod; };

    const auto testCase = GENERATE(
                TestCase{"\x08", 0},
                TestCase{"\x7f", TERMPAINT_MOD_CTRL}
    );

    std::string rawInput = testCase.rawInput;

    enum { START, GOT_EVENT } state = START;

    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_KEY);
            CHECK(event->key.modifier == testCase.mod);
            CHECK(std::string(event->key.atom) == termpaint_input_backspace());
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };

    termpaint_input *input_ctx = termpaint_input_new();
    termpaint_input_activate_quirk(input_ctx, TERMPAINT_INPUT_QUIRK_BACKSPACE_X08_AND_X7F_SWAPPED);
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    std::string input;
    termpaint_input_add_data(input_ctx, rawInput.data(), rawInput.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: events using raw") {
    struct TestCase { const std::string sequence; int type; const std::string contents; };
    const auto testCase = GENERATE(
        TestCase{ "\033P!|0\033\\", TERMPAINT_EV_RAW_3RD_DEV_ATTRIB, "0" },
        TestCase{ "\033P!|00000000\033\\", TERMPAINT_EV_RAW_3RD_DEV_ATTRIB, "00000000" },
        TestCase{ "\033P!|7E565445\033\\", TERMPAINT_EV_RAW_3RD_DEV_ATTRIB, "7E565445" },
        TestCase{ "\033[3;1;1;112;112;1;0x", TERMPAINT_EV_RAW_DECREQTPARM, "\033[3;1;1;112;112;1;0x" },
        TestCase{ "\033[3;1;1;120;120;1;0x", TERMPAINT_EV_RAW_DECREQTPARM, "\033[3;1;1;120;120;1;0x" },
        TestCase{ "\033[3;1;1;128;128;1;0x", TERMPAINT_EV_RAW_DECREQTPARM, "\033[3;1;1;128;128;1;0x" },
        TestCase{ "\033[?x", TERMPAINT_EV_RAW_DECREQTPARM, "\033[?x" },
        TestCase{ "\033[>0;115;0c", TERMPAINT_EV_RAW_SEC_DEV_ATTRIB, "\033[>0;115;0c" },
        TestCase{ "\033[>0;95;0c", TERMPAINT_EV_RAW_SEC_DEV_ATTRIB, "\033[>0;95;0c" },
        TestCase{ "\033[>1;3000;0c", TERMPAINT_EV_RAW_SEC_DEV_ATTRIB, "\033[>1;3000;0c" },
        TestCase{ "\033[>41;280;0c", TERMPAINT_EV_RAW_SEC_DEV_ATTRIB, "\033[>41;280;0c" },
        TestCase{ "\033[?6c", TERMPAINT_EV_RAW_PRI_DEV_ATTRIB, "\033[?6c" },
        TestCase{ "\033P>|fancyterm 1.23\033\\", TERMPAINT_EV_RAW_TERM_NAME, "fancyterm 1.23"},
        TestCase{ "\033P1+r544e=787465726d2d6b69747479\033\\", TERMPAINT_EV_RAW_TERMINFO_QUERY_REPLY, "1+r544e=787465726d2d6b69747479"}
    );
    enum { START, GOT_EVENT } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == testCase.type);
            REQUIRE(std::string(event->raw.string, event->raw.length) == testCase.contents);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: misc events") {
    struct TestCase { const std::string sequence; const std::string event; };
    const auto testCase = GENERATE(
        TestCase{ "\033[I", "FocusIn" },
        TestCase{ "\033[O", "FocusOut" }
    );
    enum { START, GOT_EVENT } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_MISC);
            REQUIRE(std::string(event->misc.atom, event->misc.length) == testCase.event);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: cursor position report") {
    struct TestCase { const std::string sequence; bool expect; bool safe; int x; int y; };
    const auto testCase = GENERATE(
        TestCase{ "\033[1;1R",     false, false, 0, 0 },
        TestCase{ "\033[1;2R",     true,  false, 1, 0 },
        TestCase{ "\033[1;3R",     true,  false, 2, 0 },
        TestCase{ "\033[1;4R",     true,  false, 3, 0 },
        TestCase{ "\033[1;5R",     true,  false, 4, 0 },
        TestCase{ "\033[1;6R",     true,  false, 5, 0 },
        TestCase{ "\033[1;7R",     true,  false, 6, 0 },
        TestCase{ "\033[1;8R",     true,  false, 7, 0 },
        TestCase{ "\033[1;9R",     false, false, 8, 0 },
        TestCase{ "\033[4;10R",    false, false, 9, 3 },
        TestCase{ "\033[?1;1R",    false, true,  0, 0 },
        TestCase{ "\033[?1;2R",    false, true,  1, 0 },
        TestCase{ "\033[?1;3R",    false, true,  2, 0 },
        TestCase{ "\033[?1;4R",    false, true,  3, 0 },
        TestCase{ "\033[?1;5R",    false, true,  4, 0 },
        TestCase{ "\033[?1;6R",    false, true,  5, 0 },
        TestCase{ "\033[?1;7R",    false, true,  6, 0 },
        TestCase{ "\033[?1;8R",    false, true,  7, 0 },
        TestCase{ "\033[?1;9R",    false, true,  8, 0 },
        TestCase{ "\033[?4;10R",   false, true,  9, 3 },
        TestCase{ "\033[?1;1;1R",  false, true,  0, 0 },
        TestCase{ "\033[?1;2;1R",  false, true,  1, 0 },
        TestCase{ "\033[?1;3;1R",  false, true,  2, 0 },
        TestCase{ "\033[?1;4;1R",  false, true,  3, 0 },
        TestCase{ "\033[?1;5;1R",  false, true,  4, 0 },
        TestCase{ "\033[?1;6;1R",  false, true,  5, 0 },
        TestCase{ "\033[?1;7;1R",  false, true,  6, 0 },
        TestCase{ "\033[?1;8;1R",  false, true,  7, 0 },
        TestCase{ "\033[?1;9;1R",  false, true,  8, 0 },
        TestCase{ "\033[?4;10;1R", false, true,  9, 3 }
    );
    enum { START, GOT_EVENT } state = START;
    INFO( "ESC" + testCase.sequence.substr(1));
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_CURSOR_POSITION);
            CHECK(event->cursor_position.safe == testCase.safe);
            CHECK(event->cursor_position.x == testCase.x);
            CHECK(event->cursor_position.y == testCase.y);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    if (testCase.expect) {
        termpaint_input_expect_cursor_position_report(input_ctx);
    }
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: mouse report") {
    struct TestCase { const std::string sequence; int x; int y; int raw; int action; int button; int mod; };
    const auto testCase = GENERATE(
        TestCase{ "\033[<0;1;1M",     0,   0,  0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[<0;1;1m",     0,   0,  0,   TERMPAINT_MOUSE_RELEASE, 0, 0 },
        TestCase{ "\033[32;1;1M",     0,   0,  0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[35;1;1M",     0,   0,  3,   TERMPAINT_MOUSE_RELEASE, 3, 0 },
        TestCase{ "\033[M !!",        0,   0,  0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M#!!",        0,   0,  3,   TERMPAINT_MOUSE_RELEASE, 3, 0 },
        TestCase{ "\033[<0;192;40M",  191, 39, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[<0;192;40m",  191, 39, 0,   TERMPAINT_MOUSE_RELEASE, 0, 0 },
        TestCase{ "\033[32;192;40M",  191, 39, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[35;192;40M",  191, 39, 3,   TERMPAINT_MOUSE_RELEASE, 3, 0 },
        TestCase{ "\033[M \xe0H",     191, 39, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M#\xe0H",     191, 39, 3,   TERMPAINT_MOUSE_RELEASE, 3, 0 },
        TestCase{ "\033[M àH",        191, 39, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M Hà",        39, 191, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M \xe0\xa0\x80H", 2015, 39, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M H\xe0\xa0\x80", 39, 2015, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M \xef\xbf\xbfH", 65502, 39, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M H\xef\xbf\xbf", 39, 65502, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M \xf0\x90\x80\x80H", 65503, 39, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[M H\xf0\x90\x80\x80", 39, 65503, 0,   TERMPAINT_MOUSE_PRESS,   0, 0 },
        TestCase{ "\033[<8;105;18M",  104, 17, 8,   TERMPAINT_MOUSE_PRESS,   0, TERMPAINT_MOD_ALT },
        TestCase{ "\033[<8;105;18m",  104, 17, 8,   TERMPAINT_MOUSE_RELEASE, 0, TERMPAINT_MOD_ALT },
        TestCase{ "\033[40;105;18M",  104, 17, 8,   TERMPAINT_MOUSE_PRESS,   0, TERMPAINT_MOD_ALT },
        TestCase{ "\033[43;105;18M",  104, 17, 11,  TERMPAINT_MOUSE_RELEASE, 3, TERMPAINT_MOD_ALT },
        TestCase{ "\033[<24;105;18M", 104, 17, 24,  TERMPAINT_MOUSE_PRESS,   0, TERMPAINT_MOD_ALT | TERMPAINT_MOD_CTRL },
        TestCase{ "\033[<24;105;18m", 104, 17, 24,  TERMPAINT_MOUSE_RELEASE, 0, TERMPAINT_MOD_ALT | TERMPAINT_MOD_CTRL },
        TestCase{ "\033[56;105;18M",  104, 17, 24,  TERMPAINT_MOUSE_PRESS,   0, TERMPAINT_MOD_ALT | TERMPAINT_MOD_CTRL },
        TestCase{ "\033[59;105;18M",  104, 17, 27,  TERMPAINT_MOUSE_RELEASE, 3, TERMPAINT_MOD_ALT | TERMPAINT_MOD_CTRL },
        TestCase{ "\033[<4;105;18M",  104, 17, 4,   TERMPAINT_MOUSE_PRESS,   0, TERMPAINT_MOD_SHIFT },
        TestCase{ "\033[<4;105;18m",  104, 17, 4,   TERMPAINT_MOUSE_RELEASE, 0, TERMPAINT_MOD_SHIFT },
        TestCase{ "\033[36;105;18M",  104, 17, 4,   TERMPAINT_MOUSE_PRESS,   0, TERMPAINT_MOD_SHIFT },
        TestCase{ "\033[39;105;18M",  104, 17, 7,   TERMPAINT_MOUSE_RELEASE, 3, TERMPAINT_MOD_SHIFT },
        TestCase{ "\033[<64;35;5M",   34,  4,  64,  TERMPAINT_MOUSE_PRESS,   4, 0 },
        TestCase{ "\033[<65;35;5M",   34,  4,  65,  TERMPAINT_MOUSE_PRESS,   5, 0 },
        TestCase{ "\033[96;35;5M",    34,  4,  64,  TERMPAINT_MOUSE_PRESS,   4, 0 },
        TestCase{ "\033[97;35;5M",    34,  4,  65,  TERMPAINT_MOUSE_PRESS,   5, 0 },
        TestCase{ "\033[<128;1;1M",   0,   0,  128, TERMPAINT_MOUSE_PRESS,   8, 0 },
        TestCase{ "\033[<128;1;1m",   0,   0,  128, TERMPAINT_MOUSE_RELEASE, 8, 0 },
        TestCase{ "\033[160;1;1M",    0,   0,  128, TERMPAINT_MOUSE_PRESS,   8, 0 },
        TestCase{ "\033[<129;1;1M",   0,   0,  129, TERMPAINT_MOUSE_PRESS,   9, 0 },
        TestCase{ "\033[<129;1;1m",   0,   0,  129, TERMPAINT_MOUSE_RELEASE, 9, 0 },
        TestCase{ "\033[161;1;1M",    0,   0,  129, TERMPAINT_MOUSE_PRESS,   9, 0 },
        TestCase{ "\033[M¡!!",        0,   0,  129, TERMPAINT_MOUSE_PRESS,   9, 0 },
        TestCase{ "\033[<32;35;5M",   34,  4,  32,  TERMPAINT_MOUSE_MOVE,    0, 0 },
        TestCase{ "\033[64;35;5M",    34,  4,  32,  TERMPAINT_MOUSE_MOVE,    0, 0 }
    );
    enum { START, GOT_EVENT } state = START;
    INFO( "ESC" + testCase.sequence.substr(1));
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_MOUSE);
            CHECK(event->mouse.x == testCase.x);
            CHECK(event->mouse.y == testCase.y);
            CHECK(event->mouse.raw_btn_and_flags == testCase.raw);
            CHECK(event->mouse.action == testCase.action);
            CHECK(event->mouse.button == testCase.button);
            CHECK(event->mouse.modifier == testCase.mod);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    if (testCase.sequence[2] == 'M') {
        if (testCase.sequence.size() == 6) {
            termpaint_input_expect_legacy_mouse_reports(input_ctx, TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE);
        } else {
            termpaint_input_expect_legacy_mouse_reports(input_ctx, TERMPAINT_INPUT_EXPECT_LEGACY_MOUSE_MODE_1005);
        }
    }
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: legacy mouse disable") {
    std::string sequence = "\033[M!!!";
    enum { START, GOT_UNKNOWN, GOT_BANG1, GOT_BANG2, GOT_BANG3 } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_BANG3) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_UNKNOWN);
            state = GOT_UNKNOWN;
        } else if (state == GOT_UNKNOWN) {
            REQUIRE(event->type == TERMPAINT_EV_CHAR);
            REQUIRE(event->c.length == 1);
            REQUIRE(event->c.string[0] == '!');
            state = GOT_BANG1;
        } else if (state == GOT_BANG1) {
            REQUIRE(event->type == TERMPAINT_EV_CHAR);
            REQUIRE(event->c.length == 1);
            REQUIRE(event->c.string[0] == '!');
            state = GOT_BANG2;
        } else if (state == GOT_BANG2) {
            REQUIRE(event->type == TERMPAINT_EV_CHAR);
            REQUIRE(event->c.length == 1);
            REQUIRE(event->c.string[0] == '!');
            state = GOT_BANG3;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    termpaint_input_expect_legacy_mouse_reports(input_ctx, TERMPAINT_INPUT_EXPECT_NO_LEGACY_MOUSE);
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, sequence.data(), sequence.size());
    REQUIRE(state == GOT_BANG3);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: mode report") {
    struct TestCase { const std::string sequence; bool kind; int number; int status; };
    const auto testCase = GENERATE(
        TestCase{ "\033[1;1$y",     false, 1, 1 },
        TestCase{ "\033[?1;1$y",    true,  1, 1 },
        TestCase{ "\033[?1000;4$y", true, 1000, 4 },
        TestCase{ "\033[1;0$y",     false, 1, 0 }
    );
    enum { START, GOT_EVENT } state = START;
    INFO( "ESC" + testCase.sequence.substr(1));
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_MODE_REPORT);
            CHECK(event->mode.kind == testCase.kind);
            CHECK(event->mode.number == testCase.number);
            CHECK(event->mode.status == testCase.status);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: color slot report") {
    struct TestCase { const std::string sequence; const int number; const std::string content; };
    const auto testCase = GENERATE(
        TestCase{ "\033]10;rgb:0000/0000/0000\033\\",       10,  "rgb:0000/0000/0000" },
        TestCase{ "\033]10;rgb:0000/0000/0000\007",         10,  "rgb:0000/0000/0000" },
        TestCase{ "\033]10;rgb:0000/0000/0000\x9c",         10,  "rgb:0000/0000/0000" },
        TestCase{ "\033]14;red\033\\",                      14,  "red" },
        TestCase{ "\033]14;red\007",                        14,  "red" },
        TestCase{ "\033]14;red\x9c",                        14,  "red" },
        TestCase{ "\033]17;#ffffff\033\\",                  17,  "#ffffff" },
        TestCase{ "\033]17;#ffffff\007",                    17,  "#ffffff" },
        TestCase{ "\033]19;rgba:aaaa/0000/8080/ffff\033\\", 19,  "rgba:aaaa/0000/8080/ffff" },
        TestCase{ "\033]19;rgba:aaaa/0000/8080/ffff\007",   19,  "rgba:aaaa/0000/8080/ffff" },
        TestCase{ "\033]705;CIELab:0.45/.23/.56\033\\",     705, "CIELab:0.45/.23/.56" },
        TestCase{ "\033]705;CIELab:0.45/.23/.56\007",       705, "CIELab:0.45/.23/.56" },
        TestCase{ "\033]708;#fff\033\\",                    708, "#fff" },
        TestCase{ "\033]708;#fff\007",                      708, "#fff" },
        TestCase{ "\033]708;#aaaabbbbcccc;\033\\",          708, "#aaaabbbbcccc" },
        TestCase{ "\033]708;#aaaabbbbcccc;\007",            708, "#aaaabbbbcccc" }
    );
    enum { START, GOT_EVENT } state = START;
    INFO( "ESC" + testCase.sequence.substr(1));
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_COLOR_SLOT_REPORT);
            CHECK(event->color_slot_report.slot == testCase.number);
            CHECK(std::string(event->color_slot_report.color, event->color_slot_report.length) == testCase.content);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: palette color report") {
    struct TestCase { const std::string sequence; const int number; const std::string content; };
    const auto testCase = GENERATE(
        TestCase{ "\033]4;10;rgb:0000/0000/0000\033\\",       10,  "rgb:0000/0000/0000" },
        TestCase{ "\033]4;10;rgb:0000/0000/0000\007",         10,  "rgb:0000/0000/0000" },
        TestCase{ "\033]4;10;rgb:0000/0000/0000\x9c",         10,  "rgb:0000/0000/0000" },
        TestCase{ "\033]4;14;red\033\\",                      14,  "red" },
        TestCase{ "\033]4;17;#ffffff\033\\",                  17,  "#ffffff" },
        TestCase{ "\033]4;19;rgba:aaaa/0000/8080/ffff\033\\", 19,  "rgba:aaaa/0000/8080/ffff" },
        TestCase{ "\033]4;255;CIELab:0.45/.23/.56\033\\",     255, "CIELab:0.45/.23/.56" },
        TestCase{ "\033]4;87;#fff\033\\",                     87, "#fff" },
        TestCase{ "\033]4;0;#aaaabbbbcccc;\033\\",            0, "#aaaabbbbcccc" },
        TestCase{ "\033]4;rgb:0000/0000/0000\033\\",         -1,  "rgb:0000/0000/0000" },
        TestCase{ "\033]4;rgb:0000/0000/0000\007",           -1,  "rgb:0000/0000/0000" }
    );
    enum { START, GOT_EVENT } state = START;
    INFO( "ESC" + testCase.sequence.substr(1));
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_EVENT) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_PALETTE_COLOR_REPORT);
            CHECK(event->palette_color_report.color_index == testCase.number);
            CHECK(std::string(event->palette_color_report.color_desc, event->palette_color_report.length) == testCase.content);
            state = GOT_EVENT;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    REQUIRE(state == GOT_EVENT);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: bracketed paste manual") {
    enum { START, PASTE_BEGIN, GOT_A, GOT_B, GOT_C, PASTE_END } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == PASTE_END) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_MISC);
            CHECK(event->misc.atom == termpaint_input_paste_begin());
            state = PASTE_BEGIN;
        } else if (state == PASTE_BEGIN) {
            REQUIRE(event->type == TERMPAINT_EV_CHAR);
            CHECK(event->c.length == 1);
            CHECK(event->c.string[0] == 'a');
            state = GOT_A;
        } else if (state == GOT_A) {
            REQUIRE(event->type == TERMPAINT_EV_CHAR);
            CHECK(event->c.length == 1);
            CHECK(event->c.string[0] == 'b');
            state = GOT_B;
        } else if (state == GOT_B) {
            REQUIRE(event->type == TERMPAINT_EV_CHAR);
            CHECK(event->c.length == 1);
            CHECK(event->c.string[0] == 'c');
            state = GOT_C;
        } else if (state == GOT_C) {
            REQUIRE(event->type == TERMPAINT_EV_MISC);
            CHECK(event->misc.atom == termpaint_input_paste_end());
            state = PASTE_END;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_handle_paste(input_ctx, false);
    std::string sequence = "\033[200~abc\033[201~";
    termpaint_input_add_data(input_ctx, sequence.data(), sequence.size());
    REQUIRE(state == PASTE_END);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: bracketed paste handling") {
    // this test is intentionally loose not fixing the exact distribution
    // of the sequence over events.
    std::string pasted_data;
    enum { START, PASTE_DATA, PASTE_END } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == PASTE_END) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_PASTE);
            CHECK(event->paste.initial);
            pasted_data += std::string(event->paste.string, event->paste.length);
            state = PASTE_DATA;
        } else if (state == PASTE_DATA) {
            REQUIRE(event->type == TERMPAINT_EV_PASTE);
            if (event->paste.final) {
                state = PASTE_END;
            } else {
                state = PASTE_DATA;
            }
            pasted_data += std::string(event->paste.string, event->paste.length);
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    // handle_paste is true by default
    std::string sequence = "\033[200~abc\033[201~";
    termpaint_input_add_data(input_ctx, sequence.data(), sequence.size());
    REQUIRE(state == PASTE_END);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    REQUIRE(pasted_data == "abc");
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: retriggering") {
    // test mechanism to detect end of sequences that are prefixes to other valid sequence types.
    // this also force terminates most unterminated sequences.

    struct TestCase { const std::string sequence; const std::string desc; };
    const auto testCase = GENERATE(
        TestCase{ "\033",        "escape" },
        TestCase{ "\033[",       "alt-[" },
        TestCase{ "\033O",       "alt-O" },
        TestCase{ "\033P",       "alt-P" },
        TestCase{ "\033]",       "alt-]" },

        TestCase{ "\033]stuff",  "unterminated OSC" },
        TestCase{ "\033P   \\",  "unterminated DSC with backslash" },
        TestCase{ "\033Pstuff",  "unterminated DSC" },
        TestCase{ "\033]   \\",  "unterminated OSC with backslash" },
        TestCase{ "\033[?45$",   "unterminated CSI" },
        TestCase{ "\033O45",     "unterminated SS3" },
        TestCase{ "\xc0",        "unterminated utf8 (2)" },
        TestCase{ "\xe0",        "unterminated utf8 (3)" },
        TestCase{ "\xf0",        "unterminated utf8 (4)" },
        TestCase{ "\xf8",        "unterminated utf8 (5)" },
        TestCase{ "\xfc",        "unterminated utf8 (6)" }

    );
    enum { INCOMPLETE, RETRIGGER, GOT_EVENT, GOT_RESYNC } state = INCOMPLETE;
    INFO(testCase.desc);
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_RESYNC) {
            FAIL("more events than expected");
        } else if (state == INCOMPLETE) {
            FAIL("incomplete sequence caused event");
        } else if (state == RETRIGGER) {
            if (testCase.desc == "escape") {
                REQUIRE(event->type == TERMPAINT_EV_KEY);
                REQUIRE(event->key.atom == termpaint_input_escape());
            } else if (testCase.desc == "alt-[") {
                REQUIRE(event->type == TERMPAINT_EV_CHAR);
                REQUIRE(event->c.modifier == TERMPAINT_MOD_ALT);
                REQUIRE(std::string(event->c.string, event->c.length) == "[");
            } else if (testCase.desc == "alt-O") {
                REQUIRE(event->type == TERMPAINT_EV_CHAR);
                REQUIRE(event->c.modifier == TERMPAINT_MOD_ALT);
                REQUIRE(std::string(event->c.string, event->c.length) == "O");
            } else if (testCase.desc == "alt-P") {
                REQUIRE(event->type == TERMPAINT_EV_CHAR);
                REQUIRE(event->c.modifier == TERMPAINT_MOD_ALT);
                REQUIRE(std::string(event->c.string, event->c.length) == "P");
            } else if (testCase.desc == "alt-]") {
                REQUIRE(event->type == TERMPAINT_EV_CHAR);
                REQUIRE(event->c.modifier == TERMPAINT_MOD_ALT);
                REQUIRE(std::string(event->c.string, event->c.length) == "]");
            } else {
                if (testCase.sequence[0] == '\033') {
                    REQUIRE(event->type == TERMPAINT_EV_UNKNOWN);
                } else {
                    REQUIRE(event->type == TERMPAINT_EV_INVALID_UTF8);
                }
            }
            state = GOT_EVENT;
        } else if (state == GOT_EVENT) {
            REQUIRE(event->type == TERMPAINT_EV_KEY);
            CHECK(event->key.atom == termpaint_input_i_resync());
            state = GOT_RESYNC;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    termpaint_input_add_data(input_ctx, testCase.sequence.data(), testCase.sequence.size());
    state = RETRIGGER;
    const std::string resync = "\033[0n";
    termpaint_input_add_data(input_ctx, resync.data(), resync.size());
    REQUIRE(state == GOT_RESYNC);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}

TEST_CASE("input: atoms") {
#define TEST_ATOM(name, s) CHECK(termpaint_input_ ## name() == std::string(s))
    TEST_ATOM(i_resync, "i_resync");
    TEST_ATOM(enter, "Enter");
    TEST_ATOM(space, "Space");
    TEST_ATOM(tab, "Tab");
    TEST_ATOM(backspace, "Backspace");
    TEST_ATOM(context_menu, "ContextMenu");

    TEST_ATOM(delete, "Delete");
    TEST_ATOM(end, "End");

    TEST_ATOM(home, "Home");
    TEST_ATOM(insert, "Insert");
    TEST_ATOM(page_down, "PageDown");
    TEST_ATOM(page_up, "PageUp");

    TEST_ATOM(arrow_down, "ArrowDown");
    TEST_ATOM(arrow_left, "ArrowLeft");
    TEST_ATOM(arrow_right, "ArrowRight");
    TEST_ATOM(arrow_up, "ArrowUp");

    TEST_ATOM(numpad_divide, "NumpadDivide");
    TEST_ATOM(numpad_multiply, "NumpadMultiply");
    TEST_ATOM(numpad_subtract, "NumpadSubtract");
    TEST_ATOM(numpad_add, "NumpadAdd");
    TEST_ATOM(numpad_enter, "NumpadEnter");
    TEST_ATOM(numpad_decimal, "NumpadDecimal");
    TEST_ATOM(numpad0, "Numpad0");
    TEST_ATOM(numpad1, "Numpad1");
    TEST_ATOM(numpad2, "Numpad2");
    TEST_ATOM(numpad3, "Numpad3");
    TEST_ATOM(numpad4, "Numpad4");
    TEST_ATOM(numpad5, "Numpad5");
    TEST_ATOM(numpad6, "Numpad6");
    TEST_ATOM(numpad7, "Numpad7");
    TEST_ATOM(numpad8, "Numpad8");
    TEST_ATOM(numpad9, "Numpad9");

    TEST_ATOM(escape, "Escape");

    TEST_ATOM(f1, "F1");
    TEST_ATOM(f2, "F2");
    TEST_ATOM(f3, "F3");
    TEST_ATOM(f4, "F4");
    TEST_ATOM(f5, "F5");
    TEST_ATOM(f6, "F6");
    TEST_ATOM(f7, "F7");
    TEST_ATOM(f8, "F8");
    TEST_ATOM(f9, "F9");
    TEST_ATOM(f10, "F10");
    TEST_ATOM(f11, "F11");
    TEST_ATOM(f12, "F12");

    TEST_ATOM(focus_in, "FocusIn");
    TEST_ATOM(focus_out, "FocusOut");
}

TEST_CASE("input: peek buffer") {
    std::string sequence = "\033[0n";
    enum { START, GOT_KEY } state = START;
    std::function<void(termpaint_event* event)> event_callback
            = [&] (termpaint_event* event) -> void {
        if (state == GOT_KEY) {
            FAIL("more events than expected");
        } else if (state == START) {
            REQUIRE(event->type == TERMPAINT_EV_KEY);
            REQUIRE(event->key.atom == termpaint_input_i_resync());
            state = GOT_KEY;
        } else {
            FAIL("unexpected state " << state);
        }
    };
    termpaint_input *input_ctx = termpaint_input_new();
    wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
    std::string buffer;
    for (ssize_t i = 0; i < sequence.size(); i++) {
        CAPTURE(i);
        REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == buffer.size());
        REQUIRE(std::string(termpaint_input_peek_buffer(input_ctx), buffer.size()) == buffer);
        termpaint_input_add_data(input_ctx, sequence.data() + i, 1);
        buffer.append(sequence.data() + i, 1);
    }
    REQUIRE(state == GOT_KEY);
    REQUIRE(termpaint_input_peek_buffer_length(input_ctx) == 0);
    termpaint_input_free(input_ctx);
}
