#ifndef TERMPAINT_TERMPAINT_COMPILER_INCLUDED
#define TERMPAINT_TERMPAINT_COMPILER_INCLUDED

#include <limits.h>

#if __GNUC__ >= 5
    #define BUILTIN_CHECKED_ARITHMETIC_SUPPORTED(x) 1
#else
    #ifdef __has_builtin
        #define BUILTIN_CHECKED_ARITHMETIC_SUPPORTED(x) __has_builtin(x)
    #endif
#endif
#ifndef BUILTIN_CHECKED_ARITHMETIC_SUPPORTED
    #define BUILTIN_CHECKED_ARITHMETIC_SUPPORTED(x) 0
#endif

static inline _Bool termpaint_smul_overflow(int a, int b, int* res) {
#if BUILTIN_CHECKED_ARITHMETIC_SUPPORTED(__builtin_smul_overflow)
    return __builtin_smul_overflow(a, b, res);
#else
    _Static_assert(sizeof(int) < sizeof(unsigned long long), "overflow protectiong not supported");
    *res = (unsigned int)a * (unsigned int)b;
    unsigned long long ores = (unsigned long long)a * (unsigned long long)b;
    return ores != (unsigned long long)*res;
#endif
}

static inline _Bool termpaint_sadd_overflow(int a, int b, int* res) {
#if BUILTIN_CHECKED_ARITHMETIC_SUPPORTED(__builtin_sadd_overflow)
    return __builtin_sadd_overflow(a, b, res);
#else
    _Static_assert(sizeof(int) < sizeof(long long), "overflow protectiong not supported");
    int tmp = (long long)a + (long long)b;
    *res = (int)tmp;
    return tmp > INT_MAX || tmp < INT_MIN;
#endif
}

#define UNUSED(x) (void)x

#endif
