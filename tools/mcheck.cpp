#include <string.h>
#include <functional>
#include <fstream>
#include <iostream>

typedef bool _Bool;

#include "../termpaint_input.h"

#include "../third-party/picojson.h"
#include "../third-party/docopt/docopt.h"
#include "../third-party/format.h"

using jarray = picojson::value::array;
using jobject = picojson::value::object;


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

    return state == OK;
}

void die(std::string msg) {
    std::cout << msg << std::endl;
    exit(1);
}

void check_tok(std::string filename) {
    std::ifstream istrm(filename);
    while (true) {
        std::string instr, rawstr;
        istrm >> instr;
        if (instr.empty()) {
            break;
        }
        rawstr = instr;
        while (rawstr.find("\\e") != std::string::npos)
            rawstr.replace(rawstr.find("\\e"), 2, "\e");

        if (!parses_as_one(rawstr.c_str())) {
            puts(instr.c_str());
        }
    }
}

static int hexToInt(char input) {
    if ('0' <= input && input <= '9') {
        return input - '0';
    } else if ('a' <= input && input <= 'f') {
        return input - 'a' + 10;
    } else if ('A' <= input && input <= 'F') {
        return input - 'A' + 10;
    } else {
        die("file contains unparsable hex values");
    }
    return -1;
}

void update_interpretation(std::string filename) {
    picojson::value rootval;
    {
        std::ifstream istrm(filename, std::ios::binary);
        istrm >> rootval;
        if (istrm.fail()) {
            die(fmt::format("Error while reading '{}': {}", filename, picojson::get_last_error()));
        }
    }
    jarray newSequences;
    for (const auto &caseval : rootval.get<jobject>()["sequences"].get<jarray>()) {
        jobject caseobj = caseval.get<jobject>();
        std::string keyId = caseobj["keyId"].get<std::string>();
        if (!caseobj.count("raw")) {
            newSequences.emplace_back(caseobj);
            continue;
        }
        std::string rawInputHex = caseobj["raw"].get<std::string>();
        std::string rawInput;
        for (int i=0; i < rawInputHex.size(); i+=2) {
            unsigned char ch;
            ch = (hexToInt(rawInputHex[i]) << 4) + hexToInt(rawInputHex[i+1]);
            rawInput.push_back(static_cast<char>(ch));
        }

        jobject result;
        result.emplace("keyId", keyId);
        result.emplace("raw", rawInputHex);

        enum { START, GOT_EVENT, GOT_SYNC } state = START;
        std::function<void(termpaint_event* event)> event_callback
                = [&] (termpaint_event* event) -> void {

            bool wasSync = event->type == TERMPAINT_EV_KEY && event->key.atom == termpaint_input_i_resync();
            if (state == START) {
                if (wasSync) {
                    die(fmt::format("Unable to interpret value for key '{}'. Seems to start with sync", keyId));
                }

                if (event->type == TERMPAINT_EV_UNKNOWN) {
                    result.emplace("type", "unknown");
                } else if (event->type == TERMPAINT_EV_CHAR) {
                    std::string modifiers;
                    modifiers += (event->c.modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
                    modifiers += (event->c.modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
                    modifiers += (event->c.modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";
                    result.emplace("type", "char");
                    result.emplace("mod", modifiers);
                    result.emplace("chars", std::string(event->c.string, event->c.length));
                } else if (event->type == TERMPAINT_EV_KEY) {
                    std::string modifiers;
                    modifiers += (event->key.modifier & TERMPAINT_MOD_SHIFT) ? "S" : " ";
                    modifiers += (event->key.modifier & TERMPAINT_MOD_ALT) ? "A" : " ";
                    modifiers += (event->key.modifier & TERMPAINT_MOD_CTRL) ? "C" : " ";
                    result.emplace("type", "key");
                    result.emplace("mod", modifiers);
                    result.emplace("key", event->key.atom);
                } else {
                    die(fmt::format("Unable to interpret value for key '{}'. Event type was {}", keyId, event->type));
                }

                state = GOT_EVENT;
            } else if (state == GOT_EVENT) {
                if (!wasSync) {
                    die(fmt::format("Unable to interpret value for key '{}'. Parses as multiple keys", keyId));
                }
                state = GOT_SYNC;
            } else {
                die(fmt::format("Unable to interpret value for key '{}'. State was {}", keyId, state));
            }
        };

        termpaint_input *input_ctx = termpaint_input_new();
        wrap(termpaint_input_set_event_cb, input_ctx, event_callback);
        termpaint_input_add_data(input_ctx, rawInput.data(), rawInput.size());
        if (termpaint_input_peek_buffer_length(input_ctx)) {
            termpaint_input_add_data(input_ctx, "\e[0n", 4);
        }
        newSequences.emplace_back(result);
    }
    rootval.get<jobject>()["sequences"] = picojson::value(newSequences);
    std::ofstream outputfile(filename, std::ios::binary);
    rootval.serialize(std::ostream_iterator<char>(outputfile), true);
}

static const char usage[] =
R"(Manual checking tool for libtermpaint.

    Usage:
      mcheck tok <file>
      mcheck update-interpretation <file>
      mcheck (-h | --help)
)";

int main(int argc, const char** argv)
{
    std::map<std::string, docopt::value> args = docopt::docopt(usage, { argv + 1, argv + argc });

    if (args["tok"].asBool()) {
        check_tok(args["<file>"].asString());
    }

    if (args["update-interpretation"].asBool()) {
        update_interpretation(args["<file>"].asString());
    }

    return 0;
}
