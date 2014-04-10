#include <stdio.h>
#include <assert.h>

#include "map.h"

static int limit = 10000000;

uint64_t toint(void *key)
{
        return (uint64_t)*(int *)key;
}

int main(int argc, char *argv[])
{
        map_t *m = make_map(sizeof(int), sizeof(int), toint);
        for (int i = limit; i > 0; i--) {
                mm_put(m, &i, &i);
        }
        for (int i = limit; i > 0; i--) {
                int j;
                mm_get(m, &i, &j);
                assert(j == i);
        }
        mm_print_map(m, false);
        //while(1);
        for (int i = limit; i > 0; i--) {
                mm_delete(m, &i);
        }
        mm_print_map(m, false);

        return 0;
}
