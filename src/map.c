#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TESTMAP
#include <assert.h>
#endif

#include "map.h"
#include "slice.h"
#include "link_list.h"

typedef void (*dtor)(void *);

void mm_print_map(map_t *m);

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = calloc(1, sizeof(structure))) == NULL) {           \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

static inline uint64_t h0(map_t *m, uint64_t key) {
        return key % (m->cap);
}

static inline uint64_t h1(map_t *m, uint64_t key) {
        return key % (m->cap << 1);
}

static inline uint64_t getpos(map_t *m, uint64_t key)
{
        uint64_t ret = h0(m, key);
        if (ret >= m->pos) {
                return ret;
        }
        return h1(m, key);
}

static inline float get_usage(map_t *m)
{
        return ((float)m->used
                / (float)m->s->len
                / (float)m->bucket_cap);
}

static inline bool need_split(map_t *m)
{
        return get_usage(m) > m->split_ratio;
}

static inline bool need_shrink(map_t *m)
{
        return m->cap > DEFAULT_INIT_CAP && get_usage(m) <= m->split_ratio;
}

static kv_pair_t *new_kv_pair(uint64_t key, void *value, size_t value_size)
{
        kv_pair_t *kv;
        NEW_INSTANCE(kv, kv_pair_t);
        kv->key = key;
        kv->value = malloc(value_size);
        if (!kv->value) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        memcpy(kv->value, value, value_size);
        kv->value_size = value_size;
        return kv;
}

static kv_pair_t *copy_kv(kv_pair_t *kv)
{
        return new_kv_pair(kv->key, kv->value, kv->value_size);
}

static void free_kv_pair(kv_pair_t *kv)
{
        free(kv->value);
        free(kv);
}

static kv_pair_t *get_kv_from_bucket(list_t *bucket, uint64_t key)
{
        node_t *node;
        for (ll_traverse(bucket, node)) {
                kv_pair_t *kv = (kv_pair_t *)node->item;
                if (kv->key == key) {
                        return kv;
                }
        }
        return NULL;
}

static inline list_t * get_bucket(map_t *m, uint64_t key)
{
        uint64_t offset = getpos(m, key);
        return *(list_t **)ss_getptr(m->s, offset);
}

static kv_pair_t *get_kv(map_t *m, uint64_t key)
{
        list_t * bucket = get_bucket(m, key);
        return get_kv_from_bucket(bucket, key);
}

static bool get_and_update(list_t *bucket, uint64_t key, void *value, size_t value_size)
{
        kv_pair_t *kv = get_kv_from_bucket(bucket, key);
        if (kv) {
                kv->value = realloc(kv->value, value_size);
                if (!kv->value) {
                        error_at_line(-1, errno, __FILE__, __LINE__, NULL);
                }
                memcpy(kv->value, value, value_size);
                kv->value_size = value_size;
                return true;
        }
        return false;
}

static int split(map_t *m)
{
        // allocate a new bucket to the tail of the slice
        list_t *new_bucket = ll_new_list(sizeof(kv_pair_t), (dtor_t)free_kv_pair);
        ss_append(m->s, &new_bucket);

        // split the target bucket
        list_t *split_bucket = *(list_t **)ss_getptr(m->s, m->pos);
        node_t *node;
        for (ll_traverse(split_bucket, node)) {
                kv_pair_t *kv, *kv_copy;
                kv = (kv_pair_t *)node->item;
                uint64_t new_offset = h1(m, kv->key);
                if (m->pos == new_offset) {
                        continue;
                }

                // move the item
                kv_copy = copy_kv(kv);
                new_bucket = *(list_t **)ss_getptr(m->s, new_offset);
                ll_append_ref(new_bucket, kv_copy);

                node = node->prev;
                ll_remove_node(split_bucket, node->next);
        }

        // update pos, cap
        m->pos++;
        if (m->pos == m->cap) {
                m->cap <<= 1;
                m->pos = 0;
        }
        return 0;
}

static int shrink(map_t *m)
{
        // get the original position
        uint64_t original_offset = m->pos - 1;
        if (m->pos == 0) {
                original_offset = (m->cap >> 1) - 1;
        }
        list_t * original_bucket = *(list_t **)ss_getptr(m->s, original_offset);

        // re-arrange items in the last bucket
        list_t *last_bucket = *(list_t **)ss_getptr(m->s, m->s->len-1);
        node_t *node;
        for (ll_traverse(last_bucket, node)) {
                kv_pair_t *kv, *kv_copy;
                kv = (kv_pair_t *)node->item;

                // move to its original position
                kv_copy = copy_kv(kv);
                ll_append_ref(original_bucket, kv_copy);
                node = node->prev;
                ll_remove_node(last_bucket, node->next);
        }
        ll_delete_list(last_bucket);

        // update len, pos, cap
        ss_shrink(m->s, m->s->len-1);
        if (m->pos == 0) {
                m->cap >>= 1;
                m->pos = m->cap-1;
        } else {
                m->pos--;
        }
        return 0;
}

static bool found_and_remove_from_bucket(list_t *bucket, uint64_t key)
{
        node_t *node;
        for (ll_traverse(bucket, node)) {
                kv_pair_t *kv = (kv_pair_t *)node->item;
                if (kv->key == key) {
                        ll_remove_node(bucket, node);
                        return true;
                }
        }
        return false;
}

map_t *make_map(void)
{
        map_t *m;
        NEW_INSTANCE(m, map_t);
        m->cap = DEFAULT_INIT_CAP;
        m->bucket_cap = DEFAULT_BUCKET_CAP;
        m->split_ratio = SPLIT_RATIO;
        m->pos = 0;

        slice_t *s = make_slice(m->cap, sizeof(list_t *), NULL);
        for (int i = 0; i < s->cap; i++) {
                list_t *list = ll_new_list(sizeof(kv_pair_t), (dtor)free_kv_pair);
                ss_append(s, &list);
        }

        m->s = s;
        return m;
}

bool mm_get(map_t *m, uint64_t key, void *value)
{
        kv_pair_t *kv = get_kv(m, key);
        if (kv) {
                memcpy(value, kv->value, kv->value_size);
                return true;
        }
        return false;
}

int mm_put(map_t *m, uint64_t key, void *value, size_t value_size)
{
        // update the old value if it exists
        list_t *bucket = get_bucket(m, key);
        if (get_and_update(bucket, key, value, value_size)) {
                return 0;
        }

        // otherwise, allocate a kv-pair and append it to the tail
        kv_pair_t *kv = new_kv_pair(key, value, value_size);
        ll_append_ref(bucket, kv);

        m->used++;
        if (need_split(m)) {
                split(m);
        }
        return 0;
}

bool mm_delete(map_t *m, uint64_t key)
{
        list_t *bucket = get_bucket(m, key);
        if(!found_and_remove_from_bucket(bucket, key)) {
                return false;
        }
        m->used--;
        if (need_shrink(m)) {
                shrink(m);
        }
        return true;
}

int delete_map(map_t *m)
{
        for (int i = 0; i < m->s->len; i++) {
                list_t *list = *(list_t **)ss_getptr(m->s, i);
                ll_delete_list(list);
        }
        delete_slice(m->s);
        free(m);
        return 0;
}

void mm_print_map(map_t *m)
{
        printf("map statistics:\n");
        printf("cap: %lu, used: %lu, bucket_cap: %lu, usage: %.2f, split_ratio: %.2f, pos: %lu\n",
               m->cap, m->used, m->bucket_cap, get_usage(m), m->split_ratio, m->pos);
        printf("underlying slice statistics:\n");
        printf("len: %lu, cap: %lu\n",
               m->s->len, m->s->cap);
        printf("content:\n");

        for (int i = 0; i < m->s->len; i++) {
                list_t *list = *(list_t **)ss_getptr(m->s, i);
                node_t *node;
                if (list->len > 0) {
                        printf("index[%2d]: ", i);
                }
                for (ll_traverse(list, node)) {
                        kv_pair_t *kv = (kv_pair_t *)node->item;
                        printf("[%lu] => %d ",
                               kv->key, *(int *)kv->value);
                }
                if (list->len > 0) {
                        printf("\n");
                }
        }
        printf("\n");
}

#ifdef TESTMAP

// testing
int main(int argc, char *argv[])
{
        printf("=== RUN Simple Test\n");
        map_t *m = make_map();
        //printf("after make:\n");
        //mm_print_map(m);

        for (int i = 80; i > 30; i--) {
                mm_put(m, i, &i, sizeof(int));
                //mm_print_map(m);
        }
        //printf("after insertion:\n");
        //mm_print_map(m);

        for (int i = 31; i <= 80; i++) {
                int v;
                assert(mm_get(m, i, &v));
                assert(v == i);
        }

        for (int i = 31; i < 40; i++) {
                assert(mm_delete(m, i));
        }
        //printf("after delete\n");
        //mm_print_map(m);

        for (int i = 40; i <= 80; i++) {
                assert(mm_delete(m, i));
        }
        //printf("after all delete\n");
        //mm_print_map(m);
        printf("--- PASS ---\n");

        mm_print_map(m);
        // random test
        printf("=== Random Test ===\n");
        list_t *queue = ll_new_list(sizeof(int), NULL);
        for (int i = 0; i < 102400; i++) {
                int ran = rand();
                ll_append(queue, &ran);
        }

        for (int i = 0; i < 10; i++) {
                printf("=== RUN Put Test ===\n");
                node_t *node;
                for (ll_traverse(queue, node)) {
                        uint64_t k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (uint64_t)v;

                        assert(mm_put(m, k, &v, sizeof(int)) == 0);
                        assert(mm_get(m, k, &getValue));
                        assert(getValue == v);
                }
                printf("--- PASS ---\n");

                printf("=== RUN Get Test ===\n");
                for (ll_traverse(queue, node)) {
                        uint64_t k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (uint64_t)v;

                        assert(mm_get(m, k, &getValue));
                        assert(getValue == v);
                }
                printf("--- PASS ---\n");

                printf("=== RUN Delete Test ===\n");
                for (ll_traverse(queue, node)) {
                        uint64_t k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (uint64_t)v;

                        mm_delete(m, k);
                        assert(!mm_get(m, k, &getValue));
                }
                printf("--- PASS ---\n");

                printf("=== RUN Get Test ===\n");
                for (ll_traverse(queue, node)) {
                        uint64_t k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (uint64_t)v;

                        assert(!mm_get(m, k, &getValue));
                }
                printf("--- PASS ---\n");
        }
        mm_print_map(m);

        delete_map(m);
        ll_delete_list(queue);
        return 0;
}

#endif
