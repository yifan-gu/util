#include <assert.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map.h"
#include "slice.h"
#include "link_list.h"

void mm_print_map(map_t *m, bool verbose);

int unused  __attribute__((unused));
#define DUMP_ITEM(fp, item) fwrite((item), sizeof(item), 1, (fp))
#define LOAD_ITEM(fp, item) unused = fread((item), sizeof(item), 1, (fp))

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = calloc(1, sizeof(structure))) == NULL) {           \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

static inline uint64_t h0(map_t *m, void *key) {
        return m->k2int(key) % (m->cap);
}

static inline uint64_t h1(map_t *m, void *key) {
        return m->k2int(key) % (m->cap << 1);
}

static inline uint64_t getpos(map_t *m, void *key)
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

static kv_pair_t *new_kv_pair(void *key, size_t key_size,
                              void *value, size_t value_size)
{
        kv_pair_t *kv;
        NEW_INSTANCE(kv, kv_pair_t);
        kv->key = malloc(key_size);
        if (!kv->key) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        memcpy(kv->key, key, key_size);

        kv->value = malloc(value_size);
        if (!kv->value) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        memcpy(kv->value, value, value_size);
        return kv;
}

static void free_kv_pair(kv_pair_t *kv)
{
        free(kv->key);
        free(kv->value);
        free(kv);
}

static kv_pair_t *get_kv_from_bucket(list_t *bucket, void *key, size_t key_size, keycmp_t kcmp)
{
        node_t *node;
        for (ll_traverse(bucket, node)) {
                kv_pair_t *kv = (kv_pair_t *)node->item;
                if (kcmp(kv->key, key, key_size) == 0) {
                        return kv;
                }
        }
        return NULL;
}

static inline list_t * get_bucket(map_t *m, void *key)
{
        uint64_t offset = getpos(m, key);
        return *(list_t **)ss_getptr(m->s, offset);
}

static kv_pair_t *get_kv(map_t *m, void *key)
{
        list_t * bucket = get_bucket(m, key);
        return get_kv_from_bucket(bucket, key, m->key_size, m->kcmp);
}

static bool get_and_update(map_t *m,
                           list_t *bucket,
                           void *key,
                           void *value)
{
        kv_pair_t *kv = get_kv_from_bucket(bucket, key, m->key_size, m->kcmp);
        if (kv) {
                kv->value = realloc(kv->value, m->value_size);
                if (!kv->value) {
                        error_at_line(-1, errno, __FILE__, __LINE__, NULL);
                }
                memcpy(kv->value, value, m->value_size);
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
                kv_pair_t *kv;
                kv = (kv_pair_t *)node->item;
                uint64_t new_offset = h1(m, kv->key);
                if (m->pos == new_offset) {
                        continue;
                }

                // move the node
                node_t *prev = node->prev; // need to keep this for iteration
                ll_remove_node(split_bucket, node);
                new_bucket = *(list_t **)ss_getptr(m->s, new_offset);
                ll_append_node(new_bucket, node);
                node = prev;
        }

        // update pos, cap
        m->pos++;
        if (m->pos == m->cap) {
                m->cap <<= 1;
                m->pos = 0;
        }
        return 0;
}

static inline uint64_t get_orig_pos(map_t *m)
{
        if (m->pos == 0) {
                return (m->cap >> 1) - 1;
        }
        return  m->pos - 1;
}

static int shrink(map_t *m)
{
        // compute original position
        uint64_t original_offset = get_orig_pos(m);
        list_t * original_bucket = *(list_t **)ss_getptr(m->s, original_offset);

        // re-arrange items in the last bucket
        list_t *last_bucket = *(list_t **)ss_getptr(m->s, m->s->len-1);
        node_t *node;
        for (ll_traverse(last_bucket, node)) {
                node_t *prev = node->prev; // need to keep this for iteration
                ll_remove_node(last_bucket, node);
                ll_append_node(original_bucket, node);
                node = prev;
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

static bool find_and_remove_from_bucket(list_t *bucket, void *key, size_t key_size, keycmp_t kcmp)
{
        node_t *node;
        for (ll_traverse(bucket, node)) {
                kv_pair_t *kv = (kv_pair_t *)node->item;
                if (kcmp(kv->key, key, key_size) == 0) {
                        ll_free_node(bucket, node);
                        return true;
                }
        }
        return false;
}

map_t *make_map(size_t key_size, size_t value_size, key2int_t k2int, keycmp_t kcmp)
{
        map_t *m;
        NEW_INSTANCE(m, map_t);
        m->cap = DEFAULT_INIT_CAP;
        m->bucket_cap = DEFAULT_BUCKET_CAP;
        m->split_ratio = SPLIT_RATIO;
        m->pos = 0;
        m->key_size = key_size;
        m->value_size = value_size;
        m->k2int = k2int;
        m->kcmp = kcmp;
        if (m->kcmp == NULL) {
                m->kcmp = memcmp;
        }

        slice_t *s = make_slice(m->cap, sizeof(list_t *), NULL);
        for (int i = 0; i < s->cap; i++) {
                list_t *list = ll_new_list(sizeof(kv_pair_t), (dtor_t)free_kv_pair);
                ss_append(s, &list);
        }

        m->s = s;
        return m;
}

bool mm_get(map_t *m, void *key, void *value)
{
        kv_pair_t *kv = get_kv(m, key);
        if (kv) {
                memcpy(value, kv->value, m->value_size);
                return true;
        }
        return false;
}

bool mm_haskey(map_t *m, void *key)
{
        return get_kv(m, key) != NULL;
}

int mm_put(map_t *m, void *key, void *value)
{
        // update the old value if it exists
        list_t *bucket = get_bucket(m, key);
        if (get_and_update(m, bucket, key, value)) {
                return 0;
        }

        // otherwise, allocate a kv-pair and append it to the tail
        kv_pair_t *kv = new_kv_pair(key, m->key_size, value, m->value_size);
        ll_append_ref(bucket, kv);

        m->used++;
        if (need_split(m)) {
                split(m);
        }
        return 0;
}

bool mm_delete(map_t *m, void *key)
{
        list_t *bucket = get_bucket(m, key);
        if (!find_and_remove_from_bucket(bucket, key, m->key_size, m->kcmp)) {
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

int mm_marshal(const char *path, map_t *m)
{
        FILE *fp = fopen(path, "wb+");
        if (!fp) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        // dump metadata
        DUMP_ITEM(fp, &m->bucket_cap);
        DUMP_ITEM(fp, &m->split_ratio);
        DUMP_ITEM(fp, &m->key_size);
        DUMP_ITEM(fp, &m->value_size);

        // dump the table
        for (int i = 0; i < m->s->len; i++) {
                list_t *list = *(list_t **)ss_getptr(m->s, i);
                node_t *node;
                if (list->len ==  0) {
                        continue;
                }
                for (ll_traverse(list, node)) {
                        kv_pair_t *kv = (kv_pair_t *)node->item;
                        fwrite(kv->key, m->key_size, 1, fp);
                        fwrite(kv->value, m->value_size, 1, fp);
                }
        }
        fclose(fp);
        return 0;
}

int mm_unmarshal(const char *path, map_t *m)
{
        FILE *fp = fopen(path, "rb");
        if (!fp) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        // load metadata
        LOAD_ITEM(fp, &m->bucket_cap);
        LOAD_ITEM(fp, &m->split_ratio);
        LOAD_ITEM(fp, &m->key_size);
        LOAD_ITEM(fp, &m->value_size);

        // load data
        void *key = malloc(m->key_size);
        if (!key) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        void *val = malloc(m->value_size);
        if (!val) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        while (1) {
                unused = fread(key, m->key_size, 1, fp);
                if (feof(fp)) {
                        break;
                }
                unused = fread(val, m->value_size, 1, fp);
                if (feof(fp)) {
                        break;
                }
                mm_put(m, key, val);
        }
        free(key);
        free(val);
        fclose(fp);
        return 0;
}

void mm_print_map(map_t *m, bool verbose)
{
        printf("map statistics:\n");
        printf("cap: %zu, used: %zu, bucket_cap: %zu, usage: %.2f, split_ratio: %.2f, pos: %llu\n",
               m->cap, m->used, m->bucket_cap, get_usage(m), m->split_ratio, m->pos);
        printf("underlying slice statistics:\n");
        printf("len: %zu, cap: %zu\n",
               m->s->len, m->s->cap);

        if (verbose) {
                printf("content:\n");

                for (int i = 0; i < m->s->len; i++) {
                        list_t *list = *(list_t **)ss_getptr(m->s, i);
                        node_t *node;
                        if (list->len > 0) {
                                printf("index[%2d]: ", i);
                        }
                        for (ll_traverse(list, node)) {
                                kv_pair_t *kv = (kv_pair_t *)node->item;
                                printf("[%d] => %d ",
                                       *(int *)kv->key, *(int *)kv->value);
                        }
                        if (list->len > 0) {
                                printf("\n");
                        }
                }
        }
        printf("\n");
}

#ifdef TESTMAP
// testing

uint64_t toint(const void *key)
{
        return (uint64_t)*(int *)key;
}

int main(int argc, char *argv[])
{
        ///////////////////////////////////////////////////
        //                 simple test                   //
        ///////////////////////////////////////////////////

        printf("=== RUN Simple Test\n");
        map_t *m = make_map(sizeof(int), sizeof(int), toint, NULL);
        //printf("after make:\n");
        //mm_print_map(m);

        for (int i = 80; i > 30; i--) {
                mm_put(m, &i, &i);
                //printf("i = %d\n", i);
                //mm_print_map(m, true);
        }
        //printf("after insertion:\n");
        //mm_print_map(m, true);

        for (int i = 31; i <= 80; i++) {
                int v;
                assert(mm_haskey(m, &i));
                assert(mm_get(m, &i, &v));
                assert(v == i);
        }

        for (int i = 31; i < 40; i++) {
                assert(mm_delete(m, &i));
        }
        //printf("after delete\n");
        //mm_print_map(m);

        for (int i = 40; i <= 80; i++) {
                assert(mm_delete(m, &i));
        }
        //printf("after all delete\n");
        //mm_print_map(m);
        printf("--- PASS ---\n");

        mm_print_map(m, true);

        ///////////////////////////////////////////////////
        //                 random test                   //
        ///////////////////////////////////////////////////

        printf("=== Random Test ===\n");
        list_t *queue = ll_new_list(sizeof(int), NULL);
        for (int i = 0; i < 102400; i++) {
                int ran = rand();
                ll_append(queue, &ran);
        }

        for (int i = 0; i < 1; i++) {
                printf("=== RUN Put Test ===\n");
                node_t *node;
                for (ll_traverse(queue, node)) {
                        int k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (int)v;

                        assert(mm_put(m, &k, &v) == 0);
                        assert(mm_haskey(m, &k));
                        assert(mm_get(m, &k, &getValue));
                        assert(getValue == v);
                }
                printf("--- PASS ---\n");

                printf("=== RUN Get Test ===\n");
                for (ll_traverse(queue, node)) {
                        int k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (int)v;

                        assert(mm_haskey(m, &k));
                        assert(mm_get(m, &k, &getValue));
                        assert(getValue == v);
                }
                printf("--- PASS ---\n");

                printf("=== RUN Delete Test ===\n");
                for (ll_traverse(queue, node)) {
                        int k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (int)v;

                        mm_delete(m, &k);
                        assert(!mm_haskey(m, &k));
                        assert(!mm_get(m, &k, &getValue));
                }
                printf("--- PASS ---\n");

                printf("=== RUN Get Test ===\n");
                for (ll_traverse(queue, node)) {
                        int k;
                        int v, getValue;
                        ll_get_node_item(queue, node, &v);
                        k = (int)v;

                        assert(!mm_haskey(m, &k));
                        assert(!mm_get(m, &k, &getValue));
                }
                printf("--- PASS ---\n");
        }
        //mm_print_map(m, true);

        ///////////////////////////////////////////////////
        //            marshal/unmarshl test              //
        ///////////////////////////////////////////////////

        printf("=== RUN Marshlal/Unmarshal Test ===\n");
        node_t *node;
        for (ll_traverse(queue, node)) {
                int k;
                int v, getValue;
                ll_get_node_item(queue, node, &v);
                k = (int)v;

                assert(mm_put(m, &k, &v) == 0);
                assert(mm_haskey(m, &k));
                assert(mm_get(m, &k, &getValue));
                assert(getValue == v);
        }
        //mm_print_map(m, true);

        mm_marshal("test.txt", m);
        delete_map(m);

        map_t *mm = make_map(sizeof(int), sizeof(int), toint, NULL);
        mm_unmarshal("test.txt", mm);
        //mm_print_map(mm, true);
        for (ll_traverse(queue, node)) {
                int k;
                int v, getValue;
                ll_get_node_item(queue, node, &v);
                k = (int)v;

                assert(mm_haskey(m, &k));
                assert(mm_get(mm, &k, &getValue));
                assert(getValue == v);
        }

        printf("--- PASS ---\n");

        delete_map(mm);
        ll_delete_list(queue);
        return 0;
}

#endif
