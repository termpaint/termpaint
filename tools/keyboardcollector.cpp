#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>

#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#include "termpaint.h"
#include "termpaintx.h"
#include "termpaint_input.h"

#include "../third-party/picojson.h"

using jarray = picojson::value::array;
using jobject = picojson::value::object;

static std::vector<std::string> lastRaw;
static std::vector<std::string> lastPretty;
static std::string peek_buffer;
static termpaint_surface *surface;
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

void event_handler(void *user_data, termpaint_input_event *event) {
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
    termpaint_surface_clear(surface, 0x1000000);

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

    termpaint_surface_flush(surface);
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

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    std::ifstream istrm("keyboardcollector-data.json", std::ios::binary);
    picojson::value rootval;
    istrm >> rootval;
    if (istrm.fail()) {
        std::cerr << "Error while reading keyboardcollector-data.json:" << picojson::get_last_error() << std::endl;
        return 1;
    }

    jobject root = rootval.get<jobject>();

    jobject result;

    std::cout << "What terminal are you using: " << std::endl;
    result["terminal"] = picojson::value(menu(root["terminals"].get<jarray>())["name"].get<std::string>());

    std::cout << "What test set would you like to use: " << std::endl;
    jobject testset = menu(root["testsets"].get<jarray>());
    result["testset"] = picojson::value(testset["name"].get<std::string>());

    prompts = testset["prompts"].get<jarray>();
    //prompts.erase(prompts.begin()+2, prompts.end());

    std::cout << "What terminal configuration would you like to use: " << std::endl;
    jobject mode = menu(root["modes"].get<jarray>());
    result["mode"] = picojson::value(mode["name"].get<std::string>());

    termpaint_integration *integration = termpaint_full_integration_from_fd(1, 0);
    if (!integration) {
        puts("Could not init!");
        return 1;
    }

    surface = termpaint_surface_new(integration);
    termpaint_auto_detect(surface);
    termpaint_full_integration_poll_ready(integration);
    std::string setup = mode["set"].get<std::string>();
    std::string reset = mode["reset"].get<std::string>();

    while (setup.find("\\e") != std::string::npos)
        setup.replace(setup.find("\\e"), 2, "\e");

    while (reset.find("\\e") != std::string::npos)
        reset.replace(reset.find("\\e"), 2, "\e");

    puts("\e[?1049h");
    puts(setup.data());
    fflush(stdout);

    termpaint_surface_resize(surface, 80, 24);
    termpaint_surface_clear(surface, 0x1000000);

    termpaint_surface_flush(surface);

    termpaint_input *input = termpaint_input_new();

    struct termios tattr;

    tcgetattr (STDIN_FILENO, &tattr);

    tattr.c_iflag |= IGNBRK|IGNPAR;
    tattr.c_iflag &= ~(BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF);
    tattr.c_oflag &= ~(OPOST|ONLCR|OCRNL|ONOCR|ONLRET);
    tattr.c_lflag &= ~(ICANON|IEXTEN|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;

    // ISIG, we keep ISIG set for interrupt (ctrl-C), but disable the rest.
    tattr.c_cc[VQUIT] = 0;
    tattr.c_cc[VINTR] = 0;
    tattr.c_cc[VSUSP] = 0;

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

    termpaint_input_set_raw_filter_cb(input, raw_filter, 0);
    termpaint_input_set_event_cb(input, event_handler, 0);

    render();

    while (!quit) {
        char buff[100];
        int amount = read (STDIN_FILENO, buff, 99);
        termpaint_input_add_data(input, buff, amount);
        peek_buffer = std::string(termpaint_input_peek_buffer(input), termpaint_input_peek_buffer_length(input));
        if (peek_buffer.size()) {
            write(0, "\e[5n", 4);
        }
        render();
    }

    puts(reset.data());
    puts("\e[?1049l");fflush(stdout);


    if (currentPrompt >= prompts.size()) {
        result["sequences"] = picojson::value(recordedKeypresses);
        std::ofstream outputfile("collected.json", std::ios::binary);
        picojson::value(result).serialize(std::ostream_iterator<char>(outputfile), true);
    }

    return 0;
}
