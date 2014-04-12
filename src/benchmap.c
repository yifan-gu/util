#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "map.h"

static const int limit = 1000000;
static const int trial = 10;

uint64_t toint(void *key)
{
        return (uint64_t)*(int *)key;
}

void shuffle(int s[], int len)
{
        for (int i = 0; i < len; i++) {
                int r = rand() % (len-i) + i;
                if (r == i) {
                        continue;
                }
                int tmp = s[i];
                s[i] = s[r];
                s[r] = tmp;
        }
}


int main(int argc, char *argv[])
{
        map_t *m = make_map(sizeof(int), sizeof(int), toint);
        int *s = (int *)malloc(limit * sizeof(int));
        for (int i = 0; i < limit; i++) {
                s[i] = i;
        }

        uint64_t sum = 0;
        for (int i = 0; i < trial; i++) {
                shuffle(s, limit);

                time_t now;
                time(&now);
                for (int i = 0; i < limit; i++) {
                        mm_put(m, &s[i], &s[i]);
                }
                for (int i = 0; i < limit; i++) {
                        int j;
                        mm_get(m, &s[i], &j);
                        assert(j == s[i]);
                }
                //mm_print_map(m, false);
                //while(1);
                for (int i = 0; i < limit; i++) {
                        mm_delete(m, &s[i]);
                }
                //mm_print_map(m, false);
                time_t end;
                time(&end);
                sum += end - now;
                printf("trial %d\n", i);
        }
        printf("time: %llus\n", sum);

        return 0;
}
