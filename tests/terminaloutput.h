#ifndef TERMPAINT_TERMINALOUTPUT_INCLUDED
#define TERMPAINT_TERMINALOUTPUT_INCLUDED

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

extern int driverFd;

void resetAndClear();

class Queue {
public:
    void push(std::unique_ptr<std::string>&& item) {
        std::unique_lock<std::mutex> lock(mutex);
        queue.push(std::move(item));
        lock.unlock();
        cond.notify_one();
    }

    std::unique_ptr<std::string> pop() {
        std::unique_lock<std::mutex> lock(mutex);
        while (queue.empty()) {
            cond.wait(lock);
        }
        auto item = move(queue.front());
        queue.pop();
        return item;
    }

    void clear() {
        std::unique_lock<std::mutex> lock(mutex);
        while (!queue.empty()) {
            queue.pop();
        }
    }

private:
    std::mutex mutex;
    std::condition_variable cond;
    std::queue<std::unique_ptr<std::string>> queue;
};

extern Queue queue;
extern Queue asyncQueue;

class CapturedCell {
public:
    int x;
    int y;
    std::string data;
    std::string fg, bg, deco; // empty is default (name for named colors, number for palette colors or #rrggbb
    int style = 0;
    int width = 1;

public:
    CapturedCell withFg(std::string val) {
        auto r = *this;
        r.fg = val;
        return r;
    }
    CapturedCell withBg(std::string val) {
        auto r = *this;
        r.bg = val;
        return r;
    }
    CapturedCell withDeco(std::string val) {
        auto r = *this;
        r.deco = val;
        return r;
    }
    CapturedCell withStyle(int val) {
        auto r = *this;
        r.style = val;
        return r;
    }
};

class CapturedRow {
public:
    std::vector<CapturedCell> cells;
};

class CapturedState {
public:
    std::vector<CapturedRow> rows;
    int width = -1, height = -1;
    int cursorX = -1, cursorY = -1;
    bool cursorVisible = true;
    bool cursorBlink = true;
    std::string cursorShape;
    std::string mouseMode;
    bool altScreen = false;
    bool invScreen = false;
    std::string title, iconTitle;
};

CapturedState capture();

#endif
