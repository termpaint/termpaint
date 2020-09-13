#ifndef TERMPAINT_TESTHELPER_INCLUDED
#define TERMPAINT_TESTHELPER_INCLUDED

#include <memory>

#include <termpaint.h>

#include "../third-party/catch.hpp"

template<typename T>
using cptr_DEL = void(T*);
template<typename T, cptr_DEL<T> del> struct cptr_Deleter{
    void operator()(T* t) { del(t); }
};

template<typename T, cptr_DEL<T> del>
struct unique_cptr : public std::unique_ptr<T, cptr_Deleter<T, del>> {
    operator T*() {
        return this->get();
    }
};

using terminal_uptr = unique_cptr<termpaint_terminal, termpaint_terminal_free_with_restore>;


namespace Catch {
    template<>
    struct StringMaker<std::tuple<int, int>> {
        static std::string convert(std::tuple<int, int> const& value) {
            return "{" + std::to_string(std::get<0>(value)) + ", " + std::to_string(std::get<1>(value)) + "}";
        }
    };
}

#endif
