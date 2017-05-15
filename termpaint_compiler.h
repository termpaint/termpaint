#ifndef TERMPAINT_TERMPAINT_COMPILER_INCLUDED
#define TERMPAINT_TERMPAINT_COMPILER_INCLUDED

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
    *res = (unsigned int)(a) * (unsigned int)(b);
    unsigned long long ores = (unsigned long long)a * (unsigned long long)b;
    return ores != (unsigned long long)*res;
#endif
}

#endif
