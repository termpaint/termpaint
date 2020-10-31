#include <stdlib.h>

#include "../third-party/catch.hpp"

#include <termpaint_hash.h>

template <typename X>
const unsigned char* u8p(X); // intentionally undefined

template <>
const unsigned char* u8p(const char *str) {
    return (const unsigned char*)str;
}

struct termpaint_hash_test : public termpaint_hash_item {
    int data;
};

TEST_CASE("hash: Add strings") {
    termpaint_hash* hash = static_cast<termpaint_hash*>(calloc(1, sizeof(termpaint_hash)));
    hash->item_size = sizeof(termpaint_hash_test);

    for (int i = 0; i < 128; i++) {
        std::string str = "test";
        str += std::to_string(i + 1);
        void *item = termpaintp_hash_ensure(hash, u8p(str.data()));
        REQUIRE(hash->count == i + 1);
        CHECK(item == termpaintp_hash_get(hash, u8p(str.data())));
    }

    termpaintp_hash_destroy(hash);
    free(hash);
}

TEST_CASE("hash: Add and retrieve strings") {
    termpaint_hash* hash = static_cast<termpaint_hash*>(calloc(1, sizeof(termpaint_hash)));
    hash->item_size = sizeof(termpaint_hash_test);

    static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test1")))->data = 12;
    static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test2")))->data = 42;
    static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test3")))->data = 128;

    REQUIRE(static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test2")))->data == 42);

    // grow and rehash
    for (int i = 0; i < 128; i++) {
        std::string str = "test";
        str += std::to_string(i + 1);
        termpaintp_hash_ensure(hash, u8p(str.data()));
    }

    REQUIRE(static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test2")))->data == 42);

    termpaintp_hash_destroy(hash);
    free(hash);
}

TEST_CASE("hash: GC") {
    termpaint_hash* hash = static_cast<termpaint_hash*>(calloc(1, sizeof(termpaint_hash)));
    hash->item_size = sizeof(termpaint_hash_test);
    hash->gc_mark_cb = [] (termpaint_hash* h) {
        static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(h, u8p("test1")))->unused = false;
        static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(h, u8p("test2")))->unused = false;
        static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(h, u8p("test3")))->unused = false;
    };

    static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test1")))->data = 1;
    static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test2")))->data = 2;
    static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test3")))->data = 3;

    REQUIRE(static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test2")))->data == 2);

    // add strings that are garbage collected instead of growing.
    for (int i = 0; i < 128; i++) {
        std::string str = "test";
        str += std::to_string(i + 1);
        termpaintp_hash_ensure(hash, u8p(str.data()));
    }
    CHECK(hash->allocated >= 3);
    CHECK(hash->allocated <= 32);

    REQUIRE(static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test1")))->data == 1);
    REQUIRE(static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test2")))->data == 2);
    REQUIRE(static_cast<termpaint_hash_test*>(termpaintp_hash_ensure(hash, u8p("test3")))->data == 3);

    termpaintp_hash_destroy(hash);
    free(hash);
}
