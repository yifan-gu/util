#ifndef _SLICE_H
#define _SLICE_H

#include <stdlib.h>

typedef void (*dtor_t) (void*);

typedef struct slice_s {
        void *array;

        size_t item_size;
        // length and capcity
        size_t len;
        size_t cap;

        dtor_t dtor;
}slice_t;

slice_t *make_slice(size_t cap, size_t item_size, dtor_t dtor);
size_t ss_append(slice_t *s, void *item);
void *ss_getptr(slice_t *s, uint64_t i);
int ss_get(slice_t *s, uint64_t i, void *item);
int ss_put(slice_t *s, uint64_t i, void *item);
int ss_shrink(slice_t *s, size_t new_len);
void delete_slice(slice_t *s);

#endif
