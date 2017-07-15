#include <string.h>
#include <functional>

#include "catch.hpp"

typedef bool _Bool;

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
    REQUIRE(parses_as_one(OSC7 "lsome title" ST7)); // possible reply to "\[21t"
    REQUIRE(parses_as_one(SS3_7 "P")); // F1
    REQUIRE(parses_as_one(SS3_8 "P")); // F1
}

TEST_CASE( "evil utf8" ) {
    std::vector<termpaint_input_event> events;
    unsigned num_events = 0;

    std::function<void(termpaint_input_event* event)> event_callback
            = [&] (termpaint_input_event* event) -> void {
        if (num_events < events.size()) {
            INFO("event index " << num_events);
            auto& expected = events[num_events];
            REQUIRE(expected.type == event->type);
            REQUIRE(expected.length == event->length);
            REQUIRE(memcmp(expected.atom_or_string, event->atom_or_string, expected.length) == 0);
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
        events[0].length = 1;
        events[0].atom_or_string = "\x41";
        events[1].type = TERMPAINT_EV_INVALID_UTF8;
        events[1].length = 1;
        events[1].atom_or_string = "\xc2";
        events[2].type = TERMPAINT_EV_CHAR;
        events[2].length = 1;
        events[2].atom_or_string = "\x3e";

        termpaint_input_add_data(input_ctx, input.data(), input.size());
        REQUIRE(num_events == events.size());
    }
}

// TODO malformed utf8 tests

TEST_CASE( "Overflow is handled correctly", "[overflow]" ) {
    enum { START, GOT_RAW, GOT_EVENT, ERROR } state = START;
    std::function<_Bool(const char*, unsigned, _Bool overflow)> raw_callback
            = [&state] (const char *data, unsigned length, _Bool overflow) -> _Bool {
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

    termpaint_input_event captured_event;

    std::function<void(termpaint_input_event* event)> event_callback
            = [&state, &captured_event] (termpaint_input_event* event) -> void {

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
        REQUIRE(captured_event.length == 0);
        REQUIRE(captured_event.atom_or_string == nullptr);
        REQUIRE(captured_event.modifier == 0);
    };

    SECTION( "CSI7" ) {
        input = CSI7 + std::string(2000, '1') + "A";
        runTest();
    }

    SECTION( "CSI8" ) {
        input = CSI8 + std::string(2000, '1') + "A";
        runTest();
    }

    SECTION( "SS3_7" ) {
        input = SS3_7 + std::string(2000, '1') + "A";
        runTest();
    }

    SECTION( "SS3_8" ) {
        input = SS3_8 + std::string(2000, '1') + "A";
        runTest();
    }

    SECTION( "DCS7" ) {
        input = DCS7 + std::string(2000, '1') + "A" ST7;
        runTest();
    }
    SECTION( "DCS8" ) {
        input = DCS8 + std::string(2000, '1') + "A" ST8;
        runTest();
    }
    SECTION( "OSC7" ) {
        input = OSC7 + std::string(2000, '1') + "A" ST7;
        runTest();
    }
    SECTION( "OSC8" ) {
        input = OSC8 + std::string(2000, '1') + "A" ST8;
        runTest();
    }

}
