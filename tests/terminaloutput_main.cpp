#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_NOSTDOUT

#include "../third-party/catch.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#include <third-party/picojson.h>

#include <termpaint.h>

#include "terminaloutput.h"

int driverFd;

Queue queue;
Queue asyncQueue;

void driver_quit() {
    char msg[] = "set:auto-quit";
    write(driverFd, msg, sizeof(msg));
}

namespace Catch {
    std::ostream& cout() { return std::clog; }
    std::ostream& cerr() { return std::clog; }
    std::ostream& clog() { return std::clog; }
}

void reader() {
    std::string item;
    while (true) {
        char buff[1000];
        ssize_t ret = read(driverFd, buff, sizeof(buff));
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
    char msg[] = "reset";
    write(driverFd, msg, sizeof(msg));
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
    write(driverFd, msg, sizeof(msg));

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

        auto read_flag = [](const picojson::object& obj, const char* name, int flag) {
            if (has<bool>(obj, name) && get<bool>(obj, name)) {
                return flag;
            }
            return 0;
        };

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

        ccell.x = x;
        ccell.y = y;
        ccell.data = text;

        if (state.rows.size() <= y) {
            state.rows.resize(y + 1);
        }

        state.rows[y].cells.push_back(ccell);
    }
    if (state.rows.size() != height) {
        return CapturedState{};
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

    using namespace Catch::clara;

    auto cli
      = session.cli()
          | Opt(testdriver, "testdriver")["--driver"].required();

    session.cli( cli );

    int returnCode = session.applyCommandLine( argc, argv );

    if( returnCode != 0 ) {
        return returnCode;
    }

    if (getenv("DRIVERFD")) {
        driverFd = std::atoi(getenv("DRIVERFD"));
        std::thread thr{reader};
        int retval = session.run();
        driver_quit();
        thr.detach();
        return retval;
    } else {
        auto args = std::vector<char*>{argv, argv + argc};
        args.insert(begin(args), strdup("--"));
        args.insert(begin(args), strdup("--control-via-fd0"));
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
    }

}
