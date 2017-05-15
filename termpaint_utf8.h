#ifndef TERMPAINT_TERMPAINT_UTF8_INCLUDED
#define TERMPAINT_TERMPAINT_UTF8_INCLUDED

// internal header, do api or abi stable

// return count of bytes written
// buffer needs to be 6 bytes long
// does not reject UTF-16 surrogates codepoints
static inline int termpaintp_encode_to_utf8(int codepoint, char *buf) {
#define STORE_AND_SHIFT(index)              \
    buf[index] = codepoint & 0b00111111;    \
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
        buf[0] = 0b1111100 | codepoint;
        return 6;
    } else if (codepoint >= 0x00200000) {
        STORE_AND_SHIFT(4)
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b1111000 | codepoint;
        return 5;
    } else if (codepoint >= 0x00010000) {
        STORE_AND_SHIFT(3)
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b1110000 | codepoint;
        return 4;
    } else if (codepoint >= 0x00000800) {
        STORE_AND_SHIFT(2)
        STORE_AND_SHIFT(1)
        buf[0] = 0b1111000 | codepoint;
        return 3;
    } else if (codepoint >= 0x00000080) {
        STORE_AND_SHIFT(1)
        buf[0] = 0b1111100 | codepoint;
        return 2;
    } else {
        buf[0] = codepoint;
        return 1;
    }
#undef STORE_AND_SHIFT
}

#endif
