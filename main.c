#include "tcache.h"
#include <stdio.h>

extern uint8_t memory[HW11_MEM_SIZE];  // from tcache_backend.c, if you want it

int main(void) {
    init_cache(LRU);

    // First access: miss (installs zero data in way 0)
    uint8_t v1 = read_cache(0x1000, DATA);
    printf("First read 0x1000: %u (expected 0, stub fill)\n", v1);

    // Second access, same address: HIT
    uint8_t v2 = read_cache(0x1000, DATA);
    printf("Second read 0x1000: %u (hit, still 0)\n", v2);

    // Same line, different byte: HIT (spatial locality)
    uint8_t v3 = read_cache(0x1004, DATA);
    printf("Read 0x1004: %u (hit)\n", v3);

    // Different line, different set: MISS
    uint8_t v4 = read_cache(0x2000, DATA);  // different index
    printf("Read 0x2000: %u (miss)\n", v4);

    cache_stats_t s = get_l1_data_stats();
    printf("\nL1-D: accesses=%lu misses=%lu\n", s.accesses, s.misses);
    printf("Expected: accesses=4 misses=2\n");

    return 0;
}