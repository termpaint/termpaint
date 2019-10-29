#include <random>
#include <string>

#include "../third-party/catch.hpp"

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
};

using uterminal_cptr = unique_cptr<termpaint_terminal, termpaint_terminal_free_with_restore>;

}


TEST_CASE("TT_FULL") {
    // The terminal type TT_FULL is somewhat fiction. So it can't be tested with terminal in the loop tests

    // pos1 and pos2 are queried at different time and pos1 without safe report structure.
    struct TestCase { std::string name; std::string pos1; std::string pos2; };

    struct Data {
        termpaint_integration integration;
        std::string incoming;
        std::string newData;
        int stage = 0;
        bool respondsToSecWithParam = false;
        bool respondsToTParams = false;
        TestCase testCase;
    };

    Data d;

    d.respondsToSecWithParam = GENERATE(true, false);
    d.respondsToTParams = GENERATE(true, false);

    d.testCase = GENERATE(
        TestCase{ "case 1", "\033[55;1R", "\033[?55;1R" },
        TestCase{ "ambiguous consistent", "\033[1;3R", "\033[?1;3R" },
        TestCase{ "ambiguous inconsistent", "\033[1;3R", "\033[?1;1R" }
    );

    CAPTURE(d.testCase.name);

    auto free = [] (termpaint_integration* ptr) {
        termpaint_integration_deinit(ptr);
    };

    auto write = [] (termpaint_integration* ptr, const char *data, int length) {
        Data& d = *reinterpret_cast<Data*>(ptr);
        d.incoming.append(data, length);
    };
    auto flush = [] (termpaint_integration* ptr) {
        using namespace std::literals;
        Data& d = *reinterpret_cast<Data*>(ptr);
        if (d.incoming.empty()) {
            // termpaint may do unneeded flushes
            return;
        }
        if (d.stage == -1) {
            // deinit
            return;
        }
        if (d.stage == 0 && d.incoming == "\033[5n\033[6n\033[>c\033[5n") {
            d.stage = 1;
            d.newData = "\033[0n" + d.testCase.pos1 + "\033[>0;0;0c\033[0n";
            d.incoming.clear();
        } else if (d.stage == 1 && d.incoming == "\033[=c\033[>1c\033[?6n\033[1x\033[5n") {
            d.stage = 2;
            d.newData = "\033P!|";
            std::random_device rd;
            int id;
            do {
                id = std::uniform_int_distribution<uint64_t>(1, 0xffffffff)(rd);
            } while (id == 0x7E565445 || id == 0x7E4C4E58);
            CAPTURE(id);
            char buff[10];
            sprintf(buff, "%8x", 1);
            d.newData += buff;
            d.newData += "\033\\";
            if (d.respondsToSecWithParam) {
                d.newData += "\033[>0;0;0c";
            }
            d.newData += d.testCase.pos2;
            if (d.respondsToTParams) {
                d.newData += "\033[2;1;1;128;128;1;0x";
            }
            d.newData += "\033[0n";
            d.incoming.clear();
        } else {
            d.stage = -1;
            FAIL("unexpected input: " << d.incoming);
        }

    };

    termpaint_integration_init(&d.integration, free, write, flush);
    uterminal_cptr terminal;
    terminal.reset(termpaint_terminal_new(&d.integration));
    termpaint_terminal_set_event_cb(terminal, [](void *, termpaint_event *) {}, nullptr);
    CHECK(termpaint_terminal_auto_detect_state(terminal) == termpaint_auto_detect_none);
    termpaint_terminal_auto_detect(terminal);
    CHECK(termpaint_terminal_auto_detect_state(terminal) == termpaint_auto_detect_running);
    CHECK(d.stage == 1);
    termpaint_terminal_add_input_data(terminal, d.newData.data(), d.newData.size());
    CHECK(termpaint_terminal_auto_detect_state(terminal) == termpaint_auto_detect_running);
    CHECK(d.stage == 2);
    termpaint_terminal_add_input_data(terminal, d.newData.data(), d.newData.size());
    CHECK(d.incoming == std::string());
    CHECK(termpaint_terminal_auto_detect_state(terminal) == termpaint_auto_detect_done);
    char buff[1000];
    termpaint_terminal_auto_detect_result_text(terminal, buff, sizeof(buff)-1);
    CHECK(std::string(buff) == "Type: unknown full featured(0) safe-CPR seq:>=");
    CHECK(termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_SAFE_POSITION_REPORT));
    CHECK(termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_CSI_GREATER));
    CHECK(termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_CSI_EQUALS));
    CHECK(termpaint_terminal_capable(terminal, TERMPAINT_CAPABILITY_TITLE_RESTORE));
    d.stage = -1;
}
