#define CATCH_CONFIG_MAIN

#include <string.h>
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
    std::function<_Bool(const char*, unsigned)> callback = [input, &state] (const char *data, unsigned length) -> _Bool {
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
    // TODO 8bit variant
    REQUIRE(parses_as_one(OSC7 "lsome title" ST7)); // possible reply to "\[21t"
    REQUIRE(parses_as_one(SS3_7 "P")); // F1
    REQUIRE(parses_as_one(SS3_8 "P")); // F1
}
