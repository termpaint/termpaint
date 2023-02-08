// SPDX-License-Identifier: BSL-1.0
#define CATCH_CONFIG_RUNNER
#ifndef BUNDLED_CATCH2
#ifdef CATCH3
#include "catch2/catch_all.hpp"
#else
#include "catch2/catch.hpp"
#endif
#else
#include "../third-party/catch.hpp"
#endif

int main (int argc, char * argv[]) {
    return Catch::Session().run( argc, argv );
}
