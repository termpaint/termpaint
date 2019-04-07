#ifndef TERMPAINT_TERMPAINT_HASH_INCLUDED
#define TERMPAINT_TERMPAINT_HASH_INCLUDED

#include <malloc.h>
#include <stdint.h>
#include <string.h>

// internal header, not api or abi stable

// pointers to this are stable as long as the item is kept in the hash
typedef struct termpaint_hash_item_ {
    unsigned char* text;
    bool unused;
    struct termpaint_hash_item_ *next;
} termpaint_hash_item;

typedef struct termpaint_hash_ {
    int count;
    int allocated;
    termpaint_hash_item** buckets;
    int item_size;
    void (*gc_mark_cb)(struct termpaint_hash_*);
    void (*destroy_cb)(struct termpaint_hash_*);
} termpaint_hash;


static uint32_t termpaintp_hash_fnv1a(const unsigned char* text) {
    uint32_t hash = 2166136261;
    for (; *text; ++text) {
            hash = hash ^ *text;
            hash = hash * 16777619;
    }
    return hash;
}

static void termpaintp_hash_grow(termpaint_hash* p) {
    int old_allocated = p->allocated;
    termpaint_hash_item** old_buckets = p->buckets;
    p->allocated *= 2;
    p->buckets = (termpaint_hash_item**)calloc(p->allocated, sizeof(*p->buckets));

    for (int i = 0; i < old_allocated; i++) {
        termpaint_hash_item* item_it = old_buckets[i];
        while (item_it) {
            termpaint_hash_item* item = item_it;
            item_it = item->next;

            uint32_t bucket = termpaintp_hash_fnv1a(item->text) % p->allocated;
            item->next = p->buckets[bucket];
            p->buckets[bucket] = item;
        }
    }
}

static int termpaintp_hash_gc(termpaint_hash* p) {
    if (!p->gc_mark_cb) {
        return 0;
    }

    int items_removed = 0;

    for (int i = 0; i < p->allocated; i++) {
        termpaint_hash_item* item_it = p->buckets[i];
        while (item_it) {
            item_it->unused = true;
            item_it = item_it->next;
        }
    }

    p->gc_mark_cb(p);

    for (int i = 0; i < p->allocated; i++) {
        termpaint_hash_item** prev_ptr = &p->buckets[i];
        termpaint_hash_item* item = *prev_ptr;
        while (item) {
            termpaint_hash_item* old = item;
            if (item->unused) {
                *prev_ptr = item->next;
            } else {
                prev_ptr = &item->next;
            }
            item = item->next;
            if (old->unused) {
                --p->count;
                free(old);
                ++items_removed;
            }
        }
    }
    return items_removed;
}

__attribute__ ((noinline)) static void* termpaintp_hash_ensure(termpaint_hash* p, const unsigned char* text) {
    if (!p->allocated) {
        p->allocated = 32;
        p->buckets = (termpaint_hash_item**)calloc(p->allocated, sizeof(termpaint_hash_item*));
    }
    uint32_t bucket = termpaintp_hash_fnv1a(text) % p->allocated;

    if (p->buckets[bucket]) {
        termpaint_hash_item* item = p->buckets[bucket];
        termpaint_hash_item* prev = item;
        while (item) {
            prev = item;
            if (strcmp((const char*)text, (char*)item->text) == 0) {
                return item;
            }
            item = item->next;
        }
        if (p->allocated / 2 <= p->count && termpaintp_hash_gc(p) == 0) {
            termpaintp_hash_grow(p);
            return termpaintp_hash_ensure(p, text);
        } else {
            p->count++;

            item = (termpaint_hash_item*)calloc(1, p->item_size);
            item->text = (unsigned char*)strdup((const char*)text);
            prev->next = item;
            return item;
        }
    } else {
        if (p->allocated / 2 <= p->count && termpaintp_hash_gc(p) == 0) {
            termpaintp_hash_grow(p);
            return termpaintp_hash_ensure(p, text);
        } else {
            p->count++;
            termpaint_hash_item* item = (termpaint_hash_item*)calloc(1, p->item_size);
            item->text = (unsigned char*)strdup((const char*)text);
            p->buckets[bucket] = item;
            return item;
        }
    }
}

static void termpaintp_hash_destroy(termpaint_hash* p) {
    for (int i = 0; i < p->allocated; i++) {
        termpaint_hash_item* item = p->buckets[i];
        while (item) {
            termpaint_hash_item* old = item;
            item = item->next;
            if (p->destroy_cb) {
                p->destroy_cb(old);
            }
            free(old);
        }
    }
    free(p->buckets);
    p->buckets = (termpaint_hash_item**)0;
    p->allocated = 0;
    p->count = 0;
}


#endif
