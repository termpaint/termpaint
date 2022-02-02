// SPDX-License-Identifier: BSL-1.0
#include <string.h>

#ifndef BUNDLED_CATCH2
#include "catch2/catch.hpp"
#else
#include "../third-party/catch.hpp"
#endif

typedef bool _Bool;
#include <termpaint_utf8.h>

template <typename X>
static const unsigned char* u8p(X); // intentionally undefined

template <>
const unsigned char* u8p(const char *str) {
    return (const unsigned char*)str;
}

TEST_CASE("misc invalid single byte utf8") {
    unsigned char ch;
    for (int i = 0x80; i < 0xc0; i++) {
        INFO(i);
        ch = i;
        REQUIRE(termpaintp_check_valid_sequence(&ch, 1) == false);
    }
    {
        INFO(0xfe);
        ch = 0xfe;
        REQUIRE(termpaintp_check_valid_sequence(&ch, 1) == false);
    }
    {
        INFO(0xff);
        ch = 0xff;
        REQUIRE(termpaintp_check_valid_sequence(&ch, 1) == false);
    }
}

TEST_CASE("short utf8 sequences") {
}

static inline int create_encoding_with_length(int codepoint, unsigned char *buf, int length) {
#define STORE_AND_SHIFT(index)                     \
    buf[index] = (codepoint & 0b00111111) | 0x80;    \
    codepoint = codepoint >> 6;

    if (length == 6) {
        //This is implied by the range of int:
        //REQUIRE(codepoint < (1u << (6*5+1)));
        STORE_AND_SHIFT(5)
        STORE_AND_SHIFT(4)
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11111100 | codepoint;
        return 6;
    } else if (length == 5) {
        REQUIRE(codepoint < (1 << (6*4+2)));
        STORE_AND_SHIFT(4)
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11111000 | codepoint;
        return 5;
    } else if (length == 4) {
        REQUIRE(codepoint < (1 << (6*3+3)));
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11110000 | codepoint;
        return 4;
    } else if (length == 3) {
        REQUIRE(codepoint < (1 << (6*2+4)));
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11100000 | codepoint;
        return 3;
    } else if (length == 2) {
        REQUIRE(codepoint < (1 << (6*1+5)));
        STORE_AND_SHIFT(1)
        buf[0] = 0b11000000 | codepoint;
        return 2;
    } else {
        REQUIRE(codepoint < 0x80);
        REQUIRE(length == 1);
        buf[0] = codepoint;
        return 1;
    }
#undef STORE_AND_SHIFT
}

TEST_CASE("Non shortest form") {
    SECTION ("2 byte /") {
        REQUIRE(termpaintp_check_valid_sequence(u8p("\xc1\x9c"), 2) == false);
    }
    SECTION ("2 byte A") {
        REQUIRE(termpaintp_check_valid_sequence(u8p("\xc1\x81"), 2) == false);
    }
    SECTION ("3 byte A") {
        REQUIRE(termpaintp_check_valid_sequence(u8p("\xe0\x81\x81"), 3) == false);
    }
    SECTION ("overlong nul") {
        unsigned char buffer[7];
        for (int i = 2; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0x7f") {
        unsigned char buffer[7];
        for (int i = 2; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0x7f, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0x80") {
        unsigned char buffer[7];
        for (int i = 3; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0x80, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0x7ff") {
        unsigned char buffer[7];
        for (int i = 3; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0x7ff, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0x800") {
        unsigned char buffer[7];
        for (int i = 4; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0x800, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0xffff") {
        unsigned char buffer[7];
        for (int i = 4; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0xffff, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0x10000") {
        unsigned char buffer[7];
        for (int i = 5; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0x10000, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0x1fffff") {
        unsigned char buffer[7];
        for (int i = 5; i < 7; i++) {
            INFO(i);
            create_encoding_with_length(0x1fffff, buffer, i);
            REQUIRE(termpaintp_check_valid_sequence(buffer, i) == false);
        }
    }
    SECTION ("overlong 0x200000") {
        unsigned char buffer[7];
        create_encoding_with_length(0x200000, buffer, 6);
        REQUIRE(termpaintp_check_valid_sequence(buffer, 6) == false);
    }
    SECTION ("overlong 0x3ffffff") {
        unsigned char buffer[7];
        create_encoding_with_length(0x3ffffff, buffer, 6);
        REQUIRE(termpaintp_check_valid_sequence(buffer, 6) == false);
    }
}

static void codepoint_test(unsigned lower, unsigned upper) {
    unsigned char buffer[7];
    for (unsigned codepoint = lower; codepoint <= upper; codepoint++) {
        INFO(codepoint);
        memset(buffer, 42, 7);
        int len = termpaintp_encode_to_utf8(codepoint, buffer);
        REQUIRE(buffer[len] == 42);
        REQUIRE(len == termpaintp_utf8_len(buffer[0]));
        INFO("buffer " << std::hex << (unsigned)buffer[0] << " " << (unsigned)buffer[1]
                       << " " << (unsigned)buffer[2] << " " << (unsigned)buffer[3]
                       << " " << (unsigned)buffer[4] << " " << (unsigned)buffer[5]);
        if (codepoint > 0xd7ff && codepoint < 0xe000) {
            REQUIRE(termpaintp_check_valid_sequence(buffer, len) == false);
        } else {
            REQUIRE(termpaintp_check_valid_sequence(buffer, len) == true);
        }
    }
}

TEST_CASE( "utf8 brute force unicode", "[utf8]" ) {
    codepoint_test(1, 0x10ffff);
}

TEST_CASE( "utf8 brute force", "[!hide][utf8slow]" ) {
    codepoint_test(1, 0x7fffffff);
}
