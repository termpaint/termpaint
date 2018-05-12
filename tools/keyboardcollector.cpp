#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>

#include <vector>
#include <string>
#include <functional>
#include <fstream>
#include <iostream>

#include "termpaint.h"
#include "termpaintx.h"
#include "termpaint_input.h"

#ifdef USE_SSH
#include "SshServer.h"
#endif

#include "../third-party/picojson.h"

using jarray = picojson::value::array;
using jobject = picojson::value::object;

static std::vector<std::string> lastRaw;
static std::vector<std::string> lastPretty;
static std::string peek_buffer;
static termpaint_surface *surface;
static termpaint_terminal *terminal;
static time_t last_q;
static bool quit;

static jarray prompts;
static jarray recordedKeypresses;
static int currentPrompt = 0;
static int modIndex = 0;

template <typename X>
unsigned char u8(X); // intentionally undefined

template <>
unsigned char u8(char ch) {
    return ch;
}

_Bool raw_filter(void *user_data, const char *data, unsigned length, _Bool overflow) {
    (void)user_data;
    std::string event { data, length };
    lastRaw.emplace_back(event);

    if (event == "q") {
        time_t now = time(0);
        if (last_q && (now - last_q) == 3) {
            quit = true;
        }
        last_q = now;
    } else {
        last_q = 0;
    }
    return 0;
}

void event_handler(void *user_data, termpaint_event *event) {
    (void)user_data;
    std::string pretty;

    jobject result;

    if (event->type == 0) {
        pretty = "unknown";
        result.emplace("type", "unknown");
    } else if (event->type == TERMPAINT_EV_KEY) {
        std::string modifiers;
        modifiers += (event->modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
        modifiers += (event->modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
        modifiers += (event->modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";

        pretty = "K: ";
        if ((event->modifier & ~(TERMPAINT_MOD_SHIFT|TERMPAINT_MOD_ALT|TERMPAINT_MOD_CTRL)) == 0) {
            pretty += modifiers;
        } else {
            char buf[100];
            snprintf(buf, 100, "%03d", event->modifier);
            pretty += buf;
        }
        pretty += " ";
        pretty += event->atom_or_string;

        if (event->atom_or_string != termpaint_input_i_resync()) {
            result.emplace("type", "key");
            result.emplace("mod", modifiers);
            result.emplace("key", event->atom_or_string);
        }
    } else if (event->type == TERMPAINT_EV_CHAR) {
        std::string modifiers;
        modifiers += (event->modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
        modifiers += (event->modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
        modifiers += (event->modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";

        pretty = "C: ";
        pretty += modifiers;
        pretty += " ";
        pretty += std::string { event->atom_or_string, event->length };

        result.emplace("type", "char");
        result.emplace("mod", modifiers);
        result.emplace("chars", std::string(event->atom_or_string, event->length));
    } else {
        pretty = "XXX";
    }

    if (currentPrompt < prompts.size() && result.size()) {
        std::string mod;
        jobject pobj = prompts[currentPrompt].get<jobject>();

        if (modIndex & 1) {
            mod += "S";
        }
        if (modIndex & 2) {
            mod += "A";
        }
        if (modIndex & 4) {
            mod += "C";
        }

        std::string raw;
        const char *rawChars = lastRaw.back().data();
        const unsigned char *rawUChars = reinterpret_cast<const unsigned char *>(rawChars);
        for (int i=0; i < lastRaw.back().size(); i++) {
            char buf[3];
            snprintf(buf, 3, "%02x", rawUChars[i]);
            raw += buf;
        }

        result.emplace("keyId", pobj["keyId"].get<std::string>() + "." + mod);
        result.emplace("raw", raw);
        recordedKeypresses.emplace_back(result);

        ++modIndex;
        for (auto e: pobj["skipMods"].get<jarray>()) {
            if (e.get<std::string>() == "Ctrl+Alt" && modIndex == 6) {
                ++modIndex;
            }
            if (e.get<std::string>() == "Alt" && modIndex == 2) {
                ++modIndex;
            }
            if (e.get<std::string>() == "Ctrl" && modIndex == 4) {
                ++modIndex;
            }
        }

        if (modIndex > 7) {
            modIndex = 0;
            ++currentPrompt;
            if (currentPrompt >= prompts.size()) {
                quit = true;
            }
        }
    }


    lastPretty.emplace_back(pretty);
}

void display_esc(int x, int y, const std::string &data) {
    for (unsigned i = 0; i < data.length(); i++) {
        if (u8(data[i]) == '\e') {
            termpaint_surface_write_with_colors(surface, x, y, "^[", 0xffffff, 0x7f0000);
            x+=2;
        } else if (0xfc == (0xfe & u8(data[i])) && i+5 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], data[i+4], data[i+5], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 5;
        } else if (0xf8 == (0xfc & u8(data[i])) && i+4 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], data[i+4], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 4;
        } else if (0xf0 == (0xf8 & u8(data[i])) && i+3 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], data[i+3], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 3;
        } else if (0xe0 == (0xf0 & u8(data[i])) && i+2 < data.length()) {
            char buf[7] = {data[i], data[i+1], data[i+2], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
            i += 2;
        } else if (0xc0 == (0xe0 & u8(data[i])) && i+1 < data.length()) {
            if (((unsigned char)data[i]) == 0xc2 && ((unsigned char)data[i+1]) < 0xa0) { // C1 and non breaking space
                char x = ((unsigned char)data[i+1]) >> 4;
                char a = char(x < 10 ? '0' + x : 'a' + x - 10);
                x = data[i+1] & 0xf;
                char b = char(x < 10 ? '0' + x : 'a' + x - 10);
                char buf[7] = {'\\', 'u', '0', '0', a, b, 0};
                termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f0000);
                x += 6;
            } else {
                char buf[7] = {data[i], data[i+1], 0};
                termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
                x += 1;
            }
            i += 1;
        } else if (data[i] < 32 || data[i] >= 127) {
            termpaint_surface_write_with_colors(surface, x, y, "\\x", 0xffffff, 0x7f0000);
            x += 2;
            char buf[3];
            sprintf(buf, "%02x", (unsigned char)data[i]);
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f0000);
            x += 2;
        } else {
            char buf[2] = {data[i], 0};
            termpaint_surface_write_with_colors(surface, x, y, buf, 0xffffff, 0x7f7f7f);
            x += 1;
        }
    }
}

void render() {
    termpaint_surface_clear(surface, 0x1000000, 0x1000000);

    termpaint_surface_write_with_colors(surface, 0, 0, "Keyboard collector", 0xa0a0a0, 0x1000000);

    if (currentPrompt < prompts.size()) {
        std::string out;
        jobject pobj = prompts[currentPrompt].get<jobject>();
        out = std::string("Please Press the key ");
        out += pobj["prompt"].get<std::string>();
        out += " while holding ";
        if (modIndex == 0) {
            out += "no modifier key";
        } else {
            if (modIndex & 1) {
                out += "shift ";
            }
            if (modIndex & 2) {
                out += "alt ";
            }
            if (modIndex & 4) {
                out += "ctrl";
            }
        }
        termpaint_surface_write_with_colors(surface, 0, 2, out.data(), 0xa0a0a0, 0x1000000);
        if (pobj.count("note")) {
            termpaint_surface_write_with_colors(surface, 0, 3, pobj["note"].get<std::string>().data(), 0xa0a0a0, 0x1000000);
        }

        termpaint_surface_write_with_colors(surface, 0, 5, "If the key is not accepted please press F12", 0xa0a0a0, 0x1000000);
    }

    if (peek_buffer.length()) {
        termpaint_surface_write_with_colors(surface, 0, 23, "unmatched:", 0xff0000, 0x1000000);
        display_esc(11, 23, peek_buffer);
    }

    termpaint_surface_write_with_colors(surface, 0, 14, "Last pressed keys:", 0xa0a0a0, 0x1000000);

    int y = 15;
    for (std::string &event : lastRaw) {
        display_esc(5, y, event);
        ++y;
    }

    y = 15;
    for (std::string &event : lastPretty) {
        termpaint_surface_write_with_colors(surface, 20, y, event.data(), 0xff0000, 0x1000000);
        ++y;
    }
    while (lastRaw.size() > 5) {
        lastRaw.erase(lastRaw.begin());
        lastPretty.erase(lastPretty.begin());
    }

    termpaint_terminal_flush(terminal, false);
}


jobject menu(jarray options) {
    int index = 1;
    for (auto &element : options) {
        std::cout << index << ": " << element.get<jobject>()["name"].get<std::string>() << std::endl;
        ++index;
    }
    int choice = -1;
    while (1 > choice || choice >= index) {
        std::cin >> choice;
    }
    return options[choice-1].get<jobject>();
}

#ifdef USE_SSH
class Main : public SshServer {
public:
    Main() : SshServer(2222, "collector.id_rsa") {}
    int main(std::function<bool()> poll) override;

    jobject menu(jarray options, std::function<bool()> poll) {
        int index = 1;
        for (auto &element : options) {
            outStr(std::to_string(index).c_str());
            outStr(": ");
            outStr(element.get<jobject>()["name"].get<std::string>().c_str());
            outStr("\r\n");
            ++index;
        }
        int choice = -1;
        std::string data;
        while (1 > choice || choice >= index) {
            std::string inputChars;
            termpaint_terminal_set_event_cb(terminal, [] (void * p, termpaint_event *event) {
                std::string *inputChars = static_cast<std::string*>(p);
                if (event->type == TERMPAINT_EV_CHAR) {
                    *inputChars += std::string(event->atom_or_string, event->length);
                }
                if (event->type == TERMPAINT_EV_KEY) {
                    if (event->atom_or_string == termpaint_input_enter()) {
                        *inputChars += "\n";
                    }
                }
            }, &inputChars);
            poll();

            if (inputChars.size()) {
                data.append(inputChars);
                outStr(inputChars.c_str());
                inputChars = "";
            }

            if (data.find('\n') != std::string::npos) {
                try {
                    choice = stoi(data);
                } catch (std::invalid_argument&) {
                };
                data = "";
                outStr("\r");
            }
        }
        return options[choice-1].get<jobject>();
    }

#else
class Main {
public:
    int main();
    int run() { return main(); }
    void outStr(const char *s) {
        write(0, s, strlen(s));
    }

    jobject menu(jarray options, std::function<bool()> /* not valid in this variant */) {
        int index = 1;
        for (auto &element : options) {
            std::cout << index << ": " << element.get<jobject>()["name"].get<std::string>() << std::endl;
            ++index;
        }
        int choice = -1;
        while (1 > choice || choice >= index) {
            std::cin >> choice;
        }
        return options[choice-1].get<jobject>();
    }
#endif
};


int main(int argc, char **argv) {
    (void)argc; (void)argv;

    Main m;
    m.run();
}

#ifdef USE_SSH
int Main::main(std::function<bool()> poll) {
#else
int Main::main() {
    std::function<bool()> poll;
#endif

    std::ifstream istrm("keyboardcollector-data.json", std::ios::binary);
    picojson::value rootval;
    istrm >> rootval;
    if (istrm.fail()) {
        std::cerr << "Error while reading keyboardcollector-data.json:" << picojson::get_last_error() << std::endl;
        return 1;
    }

    jobject root = rootval.get<jobject>();

    jobject result;

    outStr("What terminal are you using: \r\n");
    result["terminal"] = picojson::value(menu(root["terminals"].get<jarray>(), poll)["name"].get<std::string>());

    outStr("What test set would you like to use: \r\n");
    jobject testset = menu(root["testsets"].get<jarray>(), poll);
    result["testset"] = picojson::value(testset["name"].get<std::string>());

    prompts = testset["prompts"].get<jarray>();
    //prompts.erase(prompts.begin()+2, prompts.end());

    outStr("What terminal configuration would you like to use: \r\n");
    jobject mode = menu(root["modes"].get<jarray>(), poll);
    result["mode"] = picojson::value(mode["name"].get<std::string>());

    std::string setup = mode["set"].get<std::string>();
    std::string reset = mode["reset"].get<std::string>();

    while (setup.find("\\e") != std::string::npos)
        setup.replace(setup.find("\\e"), 2, "\e");

    while (reset.find("\\e") != std::string::npos)
        reset.replace(reset.find("\\e"), 2, "\e");

    outStr("\e[?1049h");
    outStr(setup.data());
    fflush(stdout);

#ifdef USE_SSH
    ::terminal = this->terminal;
    surface = termpaint_terminal_get_surface(this->terminal);
#else
    termpaint_integration *integration = termpaint_full_integration_from_fd(1, 0, "");
    if (!integration) {
        outStr("Could not init!");
        return 1;
    }

    terminal = termpaint_terminal_new(integration);
    termpaint_full_integration_set_terminal(integration, terminal);
    surface = termpaint_terminal_get_surface(terminal);
    //termpaint_auto_detect(surface);
    //termpaint_full_integration_wait_ready(integration);

    poll = [&] {
        bool ok = termpaint_full_integration_do_iteration(integration);
        peek_buffer = std::string(termpaint_terminal_peek_input_buffer(terminal), termpaint_terminal_peek_input_buffer_length(terminal));
        return ok;
    };

    // Settings not done in termpaint_full_integration
    struct termios tattr;
    tcgetattr (STDIN_FILENO, &tattr);
    // should not matter when ~IXON
    tattr.c_cc[VSTART] = 0;
    tattr.c_cc[VSTOP] = 0;

    // should not matter when ~ICANON|IEXTEN
    tattr.c_cc[VEOF] = 0;
    tattr.c_cc[VEOL] = 0;
    tattr.c_cc[VEOL2] = 0;
    tattr.c_cc[VERASE] = 0;
    tattr.c_cc[VKILL] = 0;
    tattr.c_cc[VREPRINT] = 0;
    tattr.c_cc[VLNEXT] = 0;
    tattr.c_cc[VWERASE] = 0;

    // misc (aka not supported under linux):
    tattr.c_cc[VDISCARD] = 0;

    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
#endif

    termpaint_surface_resize(surface, 80, 24);
    termpaint_surface_clear(surface, 0x1000000, 0x1000000);

    termpaint_terminal_flush(terminal, false);

    termpaint_terminal_set_raw_input_filter_cb(terminal, raw_filter, 0);
    termpaint_terminal_set_event_cb(terminal, event_handler, 0);

    render();

    while (!quit) {
        if (!poll()) {
            break;
        }
        render();
    }

    outStr(reset.data());
    outStr("\e[?1049l");fflush(stdout);


    if (currentPrompt >= prompts.size()) {
        result["sequences"] = picojson::value(recordedKeypresses);
        std::ofstream outputfile("collected.json", std::ios::binary);
        picojson::value(result).serialize(std::ostream_iterator<char>(outputfile), true);
    }

    return 0;
}
