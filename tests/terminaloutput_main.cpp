// SPDX-License-Identifier: BSL-1.0
#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_NOSTDOUT

#ifndef BUNDLED_CATCH2
#ifdef CATCH3
#include "catch2/catch_all.hpp"
#else
#include "catch2/catch.hpp"
#endif
#else
#include "../third-party/catch.hpp"
#endif

#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#ifndef BUNDLED_PICOJSON
#include "picojson.h"
#else
#include "../third-party/picojson.h"
#endif

#include <termpaint.h>

#include "terminaloutput.h"

int driverFd;

Queue queue;
Queue asyncQueue;

void write_or_abort(int fd, const void *buf, size_t n) {
    if (write(fd, buf, n) != (ssize_t)n) {
        abort();
    }
}

void driver_quit() {
    char msg[] = "set:auto-quit";
    write_or_abort(driverFd, msg, sizeof(msg));
}

namespace Catch {
    std::ostream& cout() { return std::clog; }
    std::ostream& cerr() { return std::clog; }
    std::ostream& clog() { return std::clog; }
}

std::atomic<bool> stopReader;

void reader() {
    std::string item;
    while (true) {
        char buff[1000];
        ssize_t ret = read(driverFd, buff, sizeof(buff));
        if (stopReader) continue; // we are stopping, just discard input
        if (ret < 0) {
            perror("reading from socket");
            std::terminate();
        }
        for (int i = 0; i < ret; i++) {
            if (buff[i] == 0) {
                if (item.size() && item[0] == '*') {
                    asyncQueue.push(std::make_unique<std::string>(item));
                } else {
                    queue.push(std::make_unique<std::string>(item));
                }
                item.clear();
            } else {
                item.push_back(buff[i]);
            }
        }
    }
}

void resetAndClear() {
    // altscreen should be disabled by reset. But somehow that doesn't seem to work.
    // So for now manually ensure altscreen is disabled.
    puts("\033[?1049l");
    char msg[] = "reset";
    write_or_abort(driverFd, msg, sizeof(msg));
    queue.pop();
    asyncQueue.clear();
}

namespace {
    template<typename T>
    bool has(const picojson::object& obj, const char* name) {
        return obj.count(name) && obj.at(name).is<T>();
    }

    template<>
    bool has<int>(const picojson::object& obj, const char* name) {
        return obj.count(name) && obj.at(name).is<double>();
    }

    template<typename T>
    T get(const picojson::object& obj, const char* name) {
        return obj.at(name).get<T>();
    }

    template<>
    int get(const picojson::object& obj, const char* name) {
        return static_cast<int>(obj.at(name).get<double>());
    }

}

CapturedState capture() {
    CapturedState state;

    char msg[] = "capture:all";
    write_or_abort(driverFd, msg, sizeof(msg));

    std::string reply = move(*queue.pop());

    picojson::value rootValue;
    std::string err;
    picojson::parse(rootValue, reply.begin(), reply.end(), &err);

    if (err.size()) {
        std::clog << "capture: " << err;
        std::terminate();
    }

    if (!rootValue.is<picojson::object>()) {
        std::clog << "capture: root is not an object";
        std::terminate();
    }
    picojson::object root = rootValue.get<picojson::object>();
    if (!has<double>(root, "version")
            || !has<double>(root, "width") || !has<double>(root, "height")
            || !has<picojson::array>(root, "cells")) {
        std::clog << "capture: root has wrong shape";
        std::terminate();
    }

    int width = static_cast<int>(get<double>(root, "width"));
    int height = static_cast<int>(get<double>(root, "height"));

    state.width = width;
    state.height = height;

    if (has<int>(root, "cursor_column")) {
        state.cursorX = get<int>(root, "cursor_column");
    }

    if (has<int>(root, "cursor_row")) {
        state.cursorY = get<int>(root, "cursor_row");
    }

    if (has<bool>(root, "cursor_visible")) {
        state.cursorVisible = get<bool>(root, "cursor_visible");
    }

    if (has<bool>(root, "cursor_blink")) {
        state.cursorBlink = get<bool>(root, "cursor_blink");
    }

    if (has<std::string>(root, "cursor_shape")) {
        state.cursorShape = get<std::string>(root, "cursor_shape");
    }

    if (has<std::string>(root, "mouse_mode")) {
        state.mouseMode = get<std::string>(root, "mouse_mode");
    }

    if (has<bool>(root, "alternate_screen")) {
        state.altScreen = get<bool>(root, "alternate_screen");
    }

    if (has<bool>(root, "inverse_screen")) {
        state.invScreen = get<bool>(root, "inverse_screen");
    }

    if (has<std::string>(root, "title")) {
        state.title = get<std::string>(root, "title");
    }

    if (has<std::string>(root, "icon_title")) {
        state.iconTitle = get<std::string>(root, "icon_title");
    }

    if (has<std::string>(root, "errors")) {
        std::string errors = get<std::string>(root, "errors");
        std::clog << "capture: terminal reported capturing errors: " << errors;
        std::terminate();
    }

    auto read_flag = [](const picojson::object& obj, const char* name, int flag) {
        if (has<bool>(obj, name) && get<bool>(obj, name)) {
            return flag;
        }
        return 0;
    };

    if (has<picojson::object>(root, "current_sgr_attr")) {
        picojson::object cell = get<picojson::object>(root, "current_sgr_attr");
        int style = 0;
        style |= read_flag(cell, "bold", TERMPAINT_STYLE_BOLD);
        style |= read_flag(cell, "italic", TERMPAINT_STYLE_ITALIC);
        style |= read_flag(cell, "blink", TERMPAINT_STYLE_BLINK);
        style |= read_flag(cell, "overline", TERMPAINT_STYLE_OVERLINE);
        style |= read_flag(cell, "inverse", TERMPAINT_STYLE_INVERSE);
        style |= read_flag(cell, "strike", TERMPAINT_STYLE_STRIKE);
        style |= read_flag(cell, "underline", TERMPAINT_STYLE_UNDERLINE);
        style |= read_flag(cell, "double_underline", TERMPAINT_STYLE_UNDERLINE_DBL);
        style |= read_flag(cell, "curly_underline", TERMPAINT_STYLE_UNDERLINE_CURLY);
        state.sgrState.style = style;

        if (has<std::string>(cell, "fg")) {
            state.sgrState.fg = get<std::string>(cell, "fg");
        }

        if (has<std::string>(cell, "bg")) {
            state.sgrState.bg = get<std::string>(cell, "bg");
        }

        if (has<std::string>(cell, "deco")) {
            state.sgrState.deco = get<std::string>(cell, "deco");
        }
    }

    picojson::array cells = get<picojson::array>(root, "cells");
    for (const auto& cellValue: cells) {
        if (!cellValue.is<picojson::object>()) {
            std::clog << "capture: cell is not an object";
            std::terminate();
        }
        picojson::object cell = cellValue.get<picojson::object>();
        if (!has<double>(cell, "x") || !has<double>(cell, "y")
                || !has<std::string>(cell, "t")) {
            std::clog << "capture: cell is missing x, y or t";
            std::terminate();
        }

        CapturedCell ccell;
        if (has<std::string>(cell, "fg")) {
            ccell.fg = get<std::string>(cell, "fg");
        }

        if (has<std::string>(cell, "bg")) {
            ccell.bg = get<std::string>(cell, "bg");
        }

        if (has<std::string>(cell, "deco")) {
            ccell.deco = get<std::string>(cell, "deco");
        }

        int style = 0;
        style |= read_flag(cell, "bold", TERMPAINT_STYLE_BOLD);
        style |= read_flag(cell, "italic", TERMPAINT_STYLE_ITALIC);
        style |= read_flag(cell, "blink", TERMPAINT_STYLE_BLINK);
        style |= read_flag(cell, "overline", TERMPAINT_STYLE_OVERLINE);
        style |= read_flag(cell, "inverse", TERMPAINT_STYLE_INVERSE);
        style |= read_flag(cell, "strike", TERMPAINT_STYLE_STRIKE);
        style |= read_flag(cell, "underline", TERMPAINT_STYLE_UNDERLINE);
        style |= read_flag(cell, "double_underline", TERMPAINT_STYLE_UNDERLINE_DBL);
        style |= read_flag(cell, "curly_underline", TERMPAINT_STYLE_UNDERLINE_CURLY);
        ccell.style = style;

        int x = static_cast<int>(get<double>(cell, "x"));
        int y = static_cast<int>(get<double>(cell, "y"));
        int width = 1;
        if (has<double>(cell, "width")) {
            width = static_cast<int>(get<double>(cell, "width"));
        }
        ccell.width = width;
        std::string text = get<std::string>(cell, "t");

        ccell.erased = has<bool>(cell, "cleared") ? get<bool>(cell, "cleared") : false;

        if (ccell.erased && text != " ") {
            std::clog << "capture: is erased but content is not space";
            std::terminate();
        }

        ccell.x = x;
        ccell.y = y;
        ccell.data = text;

        if ((int)state.rows.size() <= y) {
            state.rows.resize(y + 1);
        }

        state.rows[y].cells.push_back(ccell);
    }
    if ((int)state.rows.size() != height) {
        return CapturedState{};
    }

    if (has<picojson::object>(root, "lines")) {
        picojson::object rows = get<picojson::object>(root, "lines");
        for (const auto& row: rows) {
            char *endp = const_cast<char*>(row.first.data() + row.first.length());
            int y = strtol(row.first.data(), &endp, 10);
            if (endp != row.first.data() + row.first.length()) {
                std::clog << "capture: can not parse line key";
                std::terminate();
            }
            if (y < 0 || y >= state.height) {
                std::clog << "capture: line number out of range";
                std::terminate();
            }
            if (!row.second.is<picojson::object>()) {
                std::clog << "capture: expected type object for line";
                std::terminate();
            }
            picojson::object lineObj = row.second.get<picojson::object>();
            if (has<bool>(lineObj, "soft_wrapped")) {
                state.rows[y].softWrapped = get<bool>(lineObj, "soft_wrapped");
            }
        }
    }

    int y = 0;
    for (auto& row: state.rows) {
        std::sort(row.cells.begin(), row.cells.end(), [] (const CapturedCell &c1, const CapturedCell &c2) {
           return c1.x < c2.x;
        });
        int x = 0;
        for (auto& cell: row.cells) {
            if (cell.x != x) {
                return CapturedState{};
            }
            if (cell.y != y) {
                return CapturedState{};
            }
            x += cell.width;
        }
        if (x != width) {
            return CapturedState{};
        }
        ++y;
    }
    return state;
}

int main( int argc, char* argv[] ) {
    if (getenv("DRIVERFD")) {
        dup2(42, 2);
    }

    Catch::Session session;

    std::string testdriver;
    bool valgrind = false;

#ifdef CATCH3
    using namespace Catch::Clara;
#else
    using namespace Catch::clara;
#endif

    auto cli
      = session.cli()
          | Opt(testdriver, "testdriver")["--driver"].required()
          | Opt( valgrind )["--valgrind"];

    session.cli( cli );

    int returnCode = session.applyCommandLine( argc, argv );

    if( returnCode != 0 ) {
        return returnCode;
    }

    if (getenv("DRIVERFD")) {
        driverFd = std::atoi(getenv("DRIVERFD"));
        std::thread thr{reader};
        int retval = session.run();
        stopReader.store(true);
        driver_quit();
        thr.detach();
        return retval;
    } else {
        auto args = std::vector<char*>{argv, argv + argc};
        if (valgrind) {
            args.insert(begin(args), strdup("--log-file=testtermpaint_terminaloutput.valgrind.txt"));
            args.insert(begin(args), strdup("valgrind"));
        }
        args.insert(begin(args), strdup("--"));
        args.insert(begin(args), strdup("--control-via-fd0"));
        args.insert(begin(args), strdup("--propagate-exit-code"));
        args.insert(begin(args), strdup(testdriver.data()));
        args.push_back(nullptr);

        int fds[2];
        socketpair(AF_LOCAL, SOCK_STREAM, 0, fds); // no cloexec, these will be inherited
        dup2(fds[0], 0);
        close(fds[0]);
        dup2(2, 42);

        auto fdString = std::to_string(fds[1]);

        setenv("DRIVERFD", fdString.data(), 1);

        execv(testdriver.data(), args.data());
        return 1;
    }

}
