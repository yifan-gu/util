#ifndef _MAP_H
#define _MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "slice.h"

#ifdef TESTMAP

#define SPLIT_RATIO (0.75)
#define DEFAULT_INIT_CAP 16
#define DEFAULT_BUCKET_CAP 1

#else

#define SPLIT_RATIO (0.75)
#define DEFAULT_INIT_CAP 1024
#define DEFAULT_BUCKET_CAP 1

#endif

typedef uint64_t (*key2int_t) (const void *key);
typedef int (*keycmp_t) (const void *key1, const void *key2, size_t keysize);

typedef struct map_s {
        size_t cap;
        size_t used;
        size_t bucket_cap;
        float split_ratio;

        uint64_t pos;

        slice_t *s;
        size_t key_size;
        size_t value_size;

        key2int_t k2int;
        keycmp_t kcmp;
}map_t;

typedef struct kv_pair_s {
        void *key;
        void *value;
}kv_pair_t;

// constructor
map_t *make_map(size_t key_size, size_t value_size, key2int_t k2int, keycmp_t kcmp);

// return 0 on success, -1 on failure
int mm_put(map_t *m, void *key, void *value);

// return true if found, false if not
bool mm_get(map_t *m, void *key, void *value);

// return true if found, false if not
bool mm_haskey(map_t *m, void *key);

// return true if found, false if not
bool mm_delete(map_t *m, void *key);

int delete_map(map_t *m);

void mm_print_map(map_t *m, bool verbose);

int mm_marshal(const char *path, map_t *m);

int mm_unmarshal(const char *path, map_t *m);

#endif
