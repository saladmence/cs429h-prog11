#ifndef CACHE
#define CACHE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>

typedef enum {
    ACCESS_READ, 
    ACCESS_WRITE 
} access_type;

typedef enum { 
    RESULT_HIT, 
    RESULT_MISS 
} access_result;

typedef enum { 
    POLICY_LRU, 
    POLICY_RANDOM 
} policy;

typedef struct cache cache; 

cache* cache_create(int size_bytes, int ways, int line_size, policy policy, cache *next_level);
access_result cache_access(cache *c, uint64_t addr, access_type type);
void cache_destroy(cache *c);
void cache_print_stats(cache *c, const char *name);
int loggers(int n);

#endif