#include "cachelab.h"
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    int valid;
    unsigned long tag;
    unsigned long long lru;
} line_t;

typedef struct {
    line_t *lines; /* array of E lines */
} set_t;

typedef struct {
    int s;
    int E;
    int b;
    int S; /* number of sets = 1<<s */
    set_t *sets; /* array of S sets */
} cache_t;

static int hits = 0;
static int misses = 0;
static int evictions = 0;
static unsigned long long timer = 0;

static cache_t *cache = NULL;

static void init_cache(int s, int E, int b)
{
    int S = 1 << s;
    cache = (cache_t *)malloc(sizeof(cache_t));
    cache->s = s; cache->E = E; cache->b = b; cache->S = S;
    cache->sets = (set_t *)malloc(sizeof(set_t) * S);
    for (int i = 0; i < S; i++) {
        cache->sets[i].lines = (line_t *)malloc(sizeof(line_t) * E);
        for (int j = 0; j < E; j++) {
            cache->sets[i].lines[j].valid = 0;
            cache->sets[i].lines[j].tag = 0;
            cache->sets[i].lines[j].lru = 0;
        }
    }
}

static void free_cache()
{
    if (!cache) return;
    for (int i = 0; i < cache->S; i++) {
        free(cache->sets[i].lines);
    }
    free(cache->sets);
    free(cache);
    cache = NULL;
}

/*
 * access_data: simulate a single memory access (load/store)
 * returns: 1 = hit, 0 = miss (no eviction), 2 = miss with eviction
 */
static int access_data(unsigned long addr)
{
    unsigned long set_index = (addr >> cache->b) & ((1UL << cache->s) - 1);
    unsigned long tag = addr >> (cache->s + cache->b);
    line_t *lines = cache->sets[set_index].lines;
    int E = cache->E;

    /* check hit */
    for (int i = 0; i < E; i++) {
        if (lines[i].valid && lines[i].tag == tag) {
            hits++;
            lines[i].lru = ++timer;
            return 1;
        }
    }

    /* miss */
    misses++;

    /* find an empty line */
    for (int i = 0; i < E; i++) {
        if (!lines[i].valid) {
            lines[i].valid = 1;
            lines[i].tag = tag;
            lines[i].lru = ++timer;
            return 0;
        }
    }

    /* eviction: find LRU line */
    int lru_index = 0;
    unsigned long long min_lru = lines[0].lru;
    for (int i = 1; i < E; i++) {
        if (lines[i].lru < min_lru) {
            min_lru = lines[i].lru;
            lru_index = i;
        }
    }
    evictions++;
    lines[lru_index].tag = tag;
    lines[lru_index].lru = ++timer;
    return 2;
}

static void usage(char *prog)
{
    printf("Usage: %s [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n", prog);
}

int main(int argc, char **argv)
{
    int s = 0, E = 0, b = 0;
    char *tracefile = NULL;
    int verbose = 0;
    int opt;

    while ((opt = getopt(argc, argv, "s:E:b:t:vh")) != -1) {
        switch (opt) {
        case 's': s = atoi(optarg); break;
        case 'E': E = atoi(optarg); break;
        case 'b': b = atoi(optarg); break;
        case 't': tracefile = optarg; break;
        case 'v': verbose = 1; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }

    if (s <= 0 || E <= 0 || b < 0 || !tracefile) {
        usage(argv[0]);
        return 1;
    }

    init_cache(s, E, b);

    FILE *fp = fopen(tracefile, "r");
    if (!fp) {
        fprintf(stderr, "Error opening trace file %s\n", tracefile);
        free_cache();
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char op;
        unsigned long addr;
        int size;
        if (sscanf(line, " %c %lx,%d", &op, &addr, &size) < 1) continue;
        if (op == 'I') continue; /* ignore instruction loads */

        if (op == 'L' || op == 'S') {
            int res = access_data(addr);
            if (verbose) {
                printf("%c %lx,%d", op, addr, size);
                if (res == 1) printf(" hit");
                else if (res == 0) printf(" miss");
                else if (res == 2) printf(" miss eviction");
                printf("\n");
            }
        } else if (op == 'M') {
            int res = access_data(addr);
            if (verbose) {
                printf("%c %lx,%d", op, addr, size);
                if (res == 1) printf(" hit");
                else if (res == 0) printf(" miss");
                else if (res == 2) printf(" miss eviction");
                printf(" hit\n");
            }
            /* the modify is a load followed by a store: always one extra hit */
            hits++;
        }
    }

    fclose(fp);
    printSummary(hits, misses, evictions);
    free_cache();
    return 0;
}
