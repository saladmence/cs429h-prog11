#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include "cache.h"

typedef struct cache_line {
    bool valid;
    bool modified;
    uint64_t tag;
    uint32_t lru;
} cache_line;

typedef struct cache_set {
    cache_line* lines;
} cache_set;

typedef struct cache {
    int num_sets;
    int ways;
    int line_size;
    int offset;
    int index;
    policy policy;
    cache_set *sets;
    uint64_t accesses, misses, writebacks;
    cache* next;
} cache;

int main() {
    cache *mem = NULL;
    cache *l2 = cache_create(2 * 1024 * 1024, 4, 64, POLICY_LRU, mem);
    cache *l1d = cache_create(32 * 1024, 2, 64, POLICY_LRU, l2);
    cache *l1i = cache_create(32*1024, 1, 64, POLICY_LRU, l2);

    cache_print_stats(l1i, "L1 I-cache");
    cache_print_stats(l1d, "L1 D-cache");
    cache_print_stats(l2, "L2");

    cache_destroy(l1i);
    cache_destroy(l1d);
    cache_destroy(l2);
    return 0;
}

cache* cache_create(int size_bytes, int ways, int line_size, policy policy, cache *next_level) {
    cache *c = calloc(1, sizeof(cache));
    c->num_sets = size_bytes / (line_size * ways);
    c->sets = calloc(c->num_sets, sizeof(cache_set));
    for (int i = 0; i < c->num_sets; i++) {
        c->sets[i].lines = calloc(ways, sizeof(cache_line));
        for (int j = 0; j < ways; j++) {
            c->sets[i].lines[j].lru = j;
        }
    }
    c->next = next_level;

    c->ways = ways;
    c->line_size = line_size;
    c->offset = loggers(line_size);
    c->index = loggers(c->num_sets);
    c->policy = policy;
    c->accesses = 0; c->misses = 0; c->writebacks = 0;
    return c;
}

access_result cache_access(cache *c, uint64_t addr, access_type type) {
    uint64_t offset = addr & ((1ULL << (c->offset)) - 1);
    uint64_t index = (addr >> c->offset) & (((1ULL << c->index)) - 1);
    uint64_t tag = addr >> (c->offset + c->index);
    c->accesses += 1;

    // picking set, then getting a line and checking if there is a hit
    cache_set *set = &c->sets[index];
    for (int i = 0; i < c->ways; i++) {
        cache_line* line = &set->lines[i];
        if (line->valid && line->tag == tag) {
            if (type == ACCESS_WRITE) line->modified = true;
            uint32_t old_count = line->lru; // lru updating
            for (int w = 0; w < c->ways; w++) {
                if (set->lines[w].lru < old_count) set->lines[w].lru++;
            }
            line->lru = 0;
            return RESULT_HIT;
        }
    }

    c->misses++;
    // picking a victim
    int victim = 0, invalid = -1;
    for (int i = 0; i < c->ways; i++) {
        if (!set->lines[i].valid) {
            invalid = i;
            break;
        }
    }
    if (invalid >= 0) victim = invalid;
    else if (c->policy == POLICY_LRU) {
        for (int w = 1; w < c->ways; w++) {
            if (set->lines[w].lru > set->lines[victim].lru) victim = w;
        }
    } else victim = rand() % c->ways;

    // accessing victim line and constructing addr for writebacks if victim is dirty
    cache_line *victim_line = &set->lines[victim];
    if (victim_line->valid && victim_line->modified && c->next != NULL) {
        uint64_t writeback = (victim_line->tag << (c->offset + c->index)) | (index << c->offset);
        cache_access(c->next, writeback, ACCESS_WRITE);
        c->writebacks++;
    }

    if (c->next != NULL) cache_access(c->next, addr, ACCESS_READ); // get new line from next level

    victim_line->valid = true;
    victim_line->modified = (type == ACCESS_WRITE); // write allocate
    victim_line->tag = tag;

    uint32_t old_count = victim_line->lru; // lru updating
    for (int w = 0; w < c->ways; w++) {
        if (set->lines[w].lru < old_count) set->lines[w].lru++;
    }
    victim_line->lru = 0;
    return RESULT_MISS;
}

void cache_destroy(cache *c) {
    if (!c) return;
    for (int i = 0; i < c->num_sets; i++) free(c->sets[i].lines);
    free(c->sets);
    free(c);
}

void cache_print_stats(cache *c, const char *name) {
    double hit_rate = c->accesses > 0 ? 100.0 * (c->accesses - c->misses) / c->accesses : 0.0;
    printf("=== %s ===\n", name);
    printf("  accesses:   %lu\n", c->accesses);
    printf("  misses:     %lu\n", c->misses);
    printf("  hit rate:   %.2f%%\n", hit_rate);
    printf("  writebacks: %lu\n", c->writebacks);
}

int loggers(int n) {
    int ans = 0;
    while (n > 1) {
        n >>= 1;
        ans++;
    }
    return ans;
}