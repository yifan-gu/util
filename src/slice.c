#include <errno.h>
#include <error.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slice.h"

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = malloc(sizeof(structure))) == NULL) {              \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

slice_t *make_slice(size_t cap, size_t item_size, dtor_t dtor)
{
        slice_t *s;
        NEW_INSTANCE(s, slice_t);
        s->item_size = item_size;
        s->cap = cap;
        s->len = 0;
        s->dtor = dtor;

        s->array = malloc(item_size * cap);
        if (!s->array) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        return s;
}

size_t ss_append(slice_t *s, void *item)
{
        if (s->len < s->cap) {
                memcpy(s->array + (s->len*s->item_size), item, s->item_size);
                s->len++;
                return s->len;
        }

        // realloc
        s->cap <<= 1;
        s->array = realloc(s->array, s->item_size * s->cap);
        if (!s->array) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }

        return ss_append(s, item);
}

int ss_get(slice_t *s, uint64_t i, void *item)
{
        if (i >= s->len) {
                printf("ss_get: index out of range i %d, len %zu\n", (int)i, s->len);
                return -1;
        }

        memcpy(item, ss_getptr(s, i), s->item_size);
        return 0;
}

void *ss_getptr(slice_t *s, uint64_t i)
{
        if (i >= s->len) {
                printf("ss_getptr: index out of range i %d, len %zu\n", (int)i, s->len);
                return NULL;
        }
        return s->array+i*s->item_size;
}

int ss_put(slice_t *s, uint64_t i, void *item)
{
        if (i >= s->len) {
                printf("ss_put: index out of range i %d, len %zu\n", (int)i, s->len);
                return -1;
        }

        memcpy(ss_getptr(s, i), item, s->item_size);
        return 0;
}

int ss_shrink(slice_t *s, size_t new_len)
{
        if (s->len <= new_len) {
                printf("new_len %zu > len %zu\n", new_len, s->len);
                return -1;
        }
        s->len = new_len;
        return 0;
}

void delete_slice(slice_t *s)
{
        if (s->dtor) {
                for (int i = 0; i < s->len; i++) {
                        s->dtor(s->array+i*s->item_size);
                }
        }
        
        free(s->array);
        free(s);
}

#ifdef TESTSLICE

int main(int argc, char *argv[])
{
        slice_t *s = make_slice(5, sizeof(int), NULL);

        for (int i = 0; i < 15; i++) {
                ss_append(s, &i);
        }

        int item;
        for (int i = 0; i < s->len; i++) {
                ss_get(s, i, &item);
                printf("slice[%d] = %d\n", i, item);
        }
        printf("slice len: %zu cap: %zu\n", s->len, s->cap);

        for (int i = 0; i < s->len; i++) {
                int tmp = i*2;
                ss_put(s, i, &tmp);
                ss_get(s, i, &item);
                printf("put %d, slice[%d] = %d\n", tmp, i, item);
        }

        ss_shrink(s, 100); // print error
        ss_shrink(s, 2);

        int tmp = 42;
        ss_append(s, &tmp);
        
        for (int i = 0; i < s->len; i++) {
                ss_get(s, i, &item);
                printf("slice[%d] = %d\n", i, item);
        }
        
        delete_slice(s);

        return 0;
}

#endif
