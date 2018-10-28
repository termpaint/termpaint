#ifndef TERMPAINT_TERMPAINT_UTF8_INCLUDED
#define TERMPAINT_TERMPAINT_UTF8_INCLUDED

// internal header, not api or abi stable

/*
  x = any bit value
  y = at least one needs to be set (to detect overlong encodings which are invalid)

0x00000000 - 0x0000007F:
    0xxxxxxx (7bit)

0x00000080 - 0x000007FF:
    110yyyyx 10xxxxxx (11bit)

0x00000800 - 0x0000D7FF:
0x0000E000 - 0x0000FFFD:
    1110yyyy 10yxxxxx 10xxxxxx (16 bit)

0x00010000 - 0x001FFFFF:
    11110yyy 10yyxxxx 10xxxxxx 10xxxxxx (21bit)

0x00200000 - 0x03FFFFFF:
    111110yy 10yyyxxx 10xxxxxx 10xxxxxx 10xxxxxx (26bit)

0x04000000 - 0x7FFFFFFF:
    1111110y 10yyyyxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx (31bit)
*/

static inline int termpaintp_utf8_len(unsigned char first_byte) {
    int size;
    if (0xfc == (0xfe & first_byte)) {
        size = 6;
    } else if (0xf8 == (0xfc & first_byte)) {
        size = 5;
    } else if (0xf0 == (0xf8 & first_byte)) {
        size = 4;
    } else if (0xe0 == (0xf0 & first_byte)) {
        size = 3;
    } else if (0xc0 == (0xe0 & first_byte)) {
        size = 2;
    } else {
        size = 1;
    }
    return size;
}

// Decodes a utf8 encoded unicode codepoint.
// input MUST point to at least length readable bytes.
// length MUST be the return of termpaintp_utf8_len(*input)
static inline int termpaintp_utf8_decode_from_utf8(const unsigned char *input, int length) {
    if (length == 1) {
        return *input;
    } else if (length == 2) {
        return ((input[0] & 0x1f) << 6)
                | (input[1] & 0x3f);
    } else if (length == 3) {
        return ((input[0] & 0x0f) << (6*2))
                | ((input[1] & 0x3f) << 6)
                | (input[2] & 0x3f);
    } else if (length == 4) {
        return ((input[0] & 0x07) << (6*3))
                | ((input[1] & 0x3f) << (6*2))
                | ((input[2] & 0x3f) << 6)
                | (input[3] & 0x3f);
    } else if (length == 5) {
        return ((input[0] & 0x03) << (6*3))
                | ((input[1] & 0x3f) << (6*3))
                | ((input[2] & 0x3f) << (6*2))
                | ((input[3] & 0x3f) << 6)
                | (input[4] & 0x3f);
    } else if (length == 6) {
        return ((input[0] & 0x01) << (6*5))
                | ((input[1] & 0x3f) << (6*4))
                | ((input[2] & 0x3f) << (6*3))
                | ((input[3] & 0x3f) << (6*2))
                | ((input[4] & 0x3f) << 6)
                | (input[5] & 0x3f);
    } else {
        return 0;
    }
}

static inline _Bool termpaintp_check_valid_sequence(const unsigned char *input, unsigned length) {
#define CHECK_CONTINUATION_BYTE(index) if ((input[index] & 0xc0) != 0x80) return false;
    if (length == 0) {
        return 0;
    }
    if ((*input >= 0x80 && *input <= 0xc0) || *input == 0xfe || *input == 0xff) {
        return 0;
    }
    unsigned len = termpaintp_utf8_len(*input);
    if (len != length) {
        return 0;
    }
    if (len == 1) {
        return 1;
    } else if (len == 2) {
        CHECK_CONTINUATION_BYTE(1)
        return input[0] & 0x1e;
    } else if (len == 3) {
        CHECK_CONTINUATION_BYTE(1)
        CHECK_CONTINUATION_BYTE(2)
        //unsigned codepoint = ((input[0] & 0x0f) << 12) | ((input[1] & 0x7f) << 6) | (input[2] & 0x7f);
        //return (codepoint >= 0x800 && codepoint < 0xd800) || codepoint >= 0xe000;
        // based on table 3-7 in unicode standard (9.0)
        if (input[0] == 0xe0) {
            // all variable bits in input[0] are zero
            // most significant variable bit in input[0] must be set if this is not an overlong sequence
            return input[1] >= 0xa0;
        } else if (input[0] == 0xed) {
            // decoded value begins with 1101
            // if code continues with 1xxxxx (thus 1101 1xxxxx ...) it is surrogate
            // thus not a valid unicode scalar value
            return input[1] < 0xa0;
        } else {
            return 1;
        }
    } else if (len == 4) {
        CHECK_CONTINUATION_BYTE(1)
        CHECK_CONTINUATION_BYTE(2)
        CHECK_CONTINUATION_BYTE(3)
        return (input[0] & 0x7) + (input[1] & 0x30);
    } else if (len == 5) {
        CHECK_CONTINUATION_BYTE(1)
        CHECK_CONTINUATION_BYTE(2)
        CHECK_CONTINUATION_BYTE(3)
        CHECK_CONTINUATION_BYTE(4)
        return (input[0] & 0x3) + (input[1] & 0x38);
    } else { // len == 6
        CHECK_CONTINUATION_BYTE(1)
        CHECK_CONTINUATION_BYTE(2)
        CHECK_CONTINUATION_BYTE(3)
        CHECK_CONTINUATION_BYTE(4)
        CHECK_CONTINUATION_BYTE(5)
        return (input[0] & 0x1) + (input[1] & 0x3c);
    }
    return 0; // not reached
#undef CHECK_CONTINUATION_BYTE
}

// return count of bytes written
// buffer needs to be 6 bytes long
// does not reject UTF-16 surrogates codepoints
static inline int termpaintp_encode_to_utf8(int codepoint, unsigned char *buf) {
#define STORE_AND_SHIFT(index)                     \
    buf[index] = (codepoint & 0b00111111) | 0x80;    \
    codepoint = codepoint >> 6;

    if (codepoint > 0x7FFFFFFF) {
        // out of range
        return 0;
    } else if (codepoint >= 0x04000000) {
        STORE_AND_SHIFT(5)
        STORE_AND_SHIFT(4)
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11111100 | codepoint;
        return 6;
    } else if (codepoint >= 0x00200000) {
        STORE_AND_SHIFT(4)
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11111000 | codepoint;
        return 5;
    } else if (codepoint >= 0x00010000) {
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11110000 | codepoint;
        return 4;
    } else if (codepoint >= 0x00000800) {
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b11100000 | codepoint;
        return 3;
    } else if (codepoint >= 0x00000080) {
        STORE_AND_SHIFT(1)
        buf[0] = 0b11000000 | codepoint;
        return 2;
    } else {
        buf[0] = codepoint;
        return 1;
    }
#undef STORE_AND_SHIFT
}


// UTF-16 sneaked in here too

static inline _Bool termpaintp_utf16_is_high_surrogate(uint16_t codeunit) {
    return (codeunit >= 0xD800) && (codeunit <= 0xDBFF);
}

static inline _Bool termpaintp_utf16_is_low_surrogate(uint16_t codeunit) {
    return (codeunit >= 0xDC00) && (codeunit <= 0xDFFF);
}

static inline int termpaintp_utf16_combine(uint16_t high, uint16_t low) {
    return 0x10000 + (((high - 0xD800) << 10) | (low - 0xDC00));
}

#endif
