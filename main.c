#include "tcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* We need access to the backend memory array for seeding test values */
extern uint8_t memory[];

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-50s ", #name); \
    fflush(stdout); \
    name(); \
    printf("[PASS]\n"); \
    tests_passed++; \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("[FAIL] %s:%d: %llu != %llu\n", __FILE__, __LINE__, \
               (unsigned long long)(a), (unsigned long long)(b)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        printf("[FAIL] %s:%d: pointer is NULL\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_NULL(p) do { \
    if ((p) != NULL) { \
        printf("[FAIL] %s:%d: pointer is not NULL\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

/* ================================================================
 * Helpers
 * ================================================================ */
static void clear_memory(void) {
    memset(memory, 0, HW11_MEM_SIZE);
}

static void seed_memory(void) {
    /* Write recognizable byte patterns into memory */
    for (uint64_t i = 0; i < HW11_MEM_SIZE; i++) {
        memory[i] = (uint8_t)(i & 0xFF);
    }
}

/* ================================================================
 * UNIT TESTS
 * ================================================================ */

/* Test 1: basic read brings data into L1 and L2 */
TEST(test_basic_read) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    uint8_t val = read_cache(0x100, DATA);
    ASSERT_EQ(val, (uint8_t)(0x100 & 0xFF));

    /* Line should now be in L1D and L2 */
    cache_line_t *l1_line = get_l1_data_cache_line(0x100);
    ASSERT_NOT_NULL(l1_line);
    ASSERT_EQ(l1_line->valid, 1);
    ASSERT_EQ(l1_line->modified, 0);

    cache_line_t *l2_line = get_l2_cache_line(0x100);
    ASSERT_NOT_NULL(l2_line);
    ASSERT_EQ(l2_line->valid, 1);

    /* Stats: 1 L1D access, 1 L1D miss, 1 L2 access, 1 L2 miss */
    cache_stats_t l1d = get_l1_data_stats();
    ASSERT_EQ(l1d.accesses, 1);
    ASSERT_EQ(l1d.misses, 1);

    cache_stats_t l2s = get_l2_stats();
    ASSERT_EQ(l2s.accesses, 1);
    ASSERT_EQ(l2s.misses, 1);
}

/* Test 2: read hit after initial miss */
TEST(test_read_hit) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    read_cache(0x200, DATA);
    uint8_t val = read_cache(0x200, DATA);
    ASSERT_EQ(val, (uint8_t)(0x200 & 0xFF));

    cache_stats_t l1d = get_l1_data_stats();
    ASSERT_EQ(l1d.accesses, 2);
    ASSERT_EQ(l1d.misses, 1);

    /* Second read should not generate another L2 access */
    cache_stats_t l2s = get_l2_stats();
    ASSERT_EQ(l2s.accesses, 1);
}

/* Test 3: write sets data and marks line modified */
TEST(test_basic_write) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    write_cache(0x300, 0xAB, DATA);
    uint8_t val = read_cache(0x300, DATA);
    ASSERT_EQ(val, 0xAB);

    cache_line_t *line = get_l1_data_cache_line(0x300);
    ASSERT_NOT_NULL(line);
    ASSERT_EQ(line->modified, 1);

    cache_stats_t l1d = get_l1_data_stats();
    /* write miss + read hit = 2 accesses, 1 miss */
    ASSERT_EQ(l1d.accesses, 2);
    ASSERT_EQ(l1d.misses, 1);
}

/* Test 4: write-back on L1 eviction */
TEST(test_writeback_on_eviction) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    /* L1D is 2-way, 256 sets. Two addresses mapping to the same set
       are 256 * 64 = 16384 (0x4000) apart in their index bits. */
    uint64_t addr_a = 0x0;
    uint64_t addr_b = addr_a + 0x4000;       /* same L1D set, different tag */
    uint64_t addr_c = addr_a + 0x4000 * 2;   /* same L1D set, evicts LRU */

    write_cache(addr_a, 0x11, DATA);
    write_cache(addr_b, 0x22, DATA);
    /* Both ways now occupied, addr_a is LRU */
    write_cache(addr_c, 0x33, DATA);  /* evicts addr_a -> writeback to L2 */

    /* addr_a should no longer be in L1D */
    ASSERT_NULL(get_l1_data_cache_line(addr_a));

    /* addr_a should be dirty in L2 with value 0x11 */
    cache_line_t *l2_line = get_l2_cache_line(addr_a);
    ASSERT_NOT_NULL(l2_line);
    ASSERT_EQ(l2_line->data[0], 0x11);
    ASSERT_EQ(l2_line->modified, 1);
}

/* Test 5: instruction cache works independently */
TEST(test_instr_cache) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    uint8_t val = read_cache(0x400, INSTR);
    ASSERT_EQ(val, (uint8_t)(0x400 & 0xFF));

    cache_line_t *l1i = get_l1_instr_cache_line(0x400);
    ASSERT_NOT_NULL(l1i);

    /* Should not be in L1D */
    ASSERT_NULL(get_l1_data_cache_line(0x400));

    cache_stats_t instr_stats = get_l1_instr_stats();
    ASSERT_EQ(instr_stats.accesses, 1);
    ASSERT_EQ(instr_stats.misses, 1);

    cache_stats_t data_stats = get_l1_data_stats();
    ASSERT_EQ(data_stats.accesses, 0);
}

/* Test 6: L1 coherency — write to DATA, read from INSTR gets new value */
TEST(test_l1_coherency) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    /* Write via DATA cache — coherency flushes to L2 and invalidates L1I */
    write_cache(0x500, 0xFE, DATA);

    /* L1D has the block (dirty flushed to L2, but still valid clean in L1D) */
    ASSERT_NOT_NULL(get_l1_data_cache_line(0x500));

    /* Read via INSTR cache — misses L1I, fetches from L2 (which has 0xFE) */
    uint8_t ival = read_cache(0x500, INSTR);
    ASSERT_EQ(ival, 0xFE);

    /* Both caches now have the block (reads don't invalidate other L1) */
    ASSERT_NOT_NULL(get_l1_instr_cache_line(0x500));
    ASSERT_NOT_NULL(get_l1_data_cache_line(0x500));
}

/* Test 7: reading different bytes in same block */
TEST(test_same_block_different_offsets) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    /* addr 0x640 and 0x641 are in the same 64-byte block (block at 0x640) */
    uint8_t v0 = read_cache(0x640, DATA);
    uint8_t v1 = read_cache(0x641, DATA);
    ASSERT_EQ(v0, (uint8_t)(0x640 & 0xFF));
    ASSERT_EQ(v1, (uint8_t)(0x641 & 0xFF));

    /* Only one miss for the block */
    cache_stats_t l1d = get_l1_data_stats();
    ASSERT_EQ(l1d.accesses, 2);
    ASSERT_EQ(l1d.misses, 1);
}

/* Test 8: L2 inclusive eviction invalidates L1 */
TEST(test_l2_inclusive_eviction) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    /* L2 is 4-way, 8192 sets. Addresses mapping to the same L2 set
       are 8192 * 64 = 524288 (0x80000) apart. We need 5 addresses
       to evict from a 4-way set. */
    uint64_t base = 0x0;
    uint64_t stride = 0x80000; /* same L2 set */

    /* Fill all 4 ways in L2 for this set */
    for (int i = 0; i < 4; i++) {
        read_cache(base + stride * i, DATA);
    }

    /* The first address should be in L2 */
    ASSERT_NOT_NULL(get_l2_cache_line(base));

    /* 5th address evicts LRU (base) from L2 */
    read_cache(base + stride * 4, DATA);

    /* base should be evicted from L2 AND L1 (inclusive) */
    ASSERT_NULL(get_l2_cache_line(base));
    ASSERT_NULL(get_l1_data_cache_line(base));
}

/* Test 9: LRU ordering — access order matters */
TEST(test_lru_order) {
    clear_memory();
    seed_memory();
    init_cache(LRU);

    /* L1D: 2-way, 256 sets. Stride for same set = 0x4000 */
    uint64_t a = 0x0;
    uint64_t b = a + 0x4000;
    uint64_t c = a + 0x4000 * 2;

    read_cache(a, DATA);   /* a is in way, LRU order: a */
    read_cache(b, DATA);   /* b is in way, LRU order: a < b */
    read_cache(a, DATA);   /* touch a, LRU order: b < a */
    read_cache(c, DATA);   /* evicts b (LRU), not a */

    ASSERT_NOT_NULL(get_l1_data_cache_line(a));
    ASSERT_NULL(get_l1_data_cache_line(b));
    ASSERT_NOT_NULL(get_l1_data_cache_line(c));
}

/* Test 10: write-back reaches memory on full eviction chain */
TEST(test_writeback_reaches_memory) {
    clear_memory();
    init_cache(LRU);

    /* Write a value */
    write_cache(0x0, 0xDD, DATA);
    ASSERT_EQ(memory[0], 0);  /* write-back: not in memory yet */

    /* Force eviction from L1D (2-way) */
    uint64_t stride_l1d = 0x4000;
    read_cache(stride_l1d, DATA);
    read_cache(stride_l1d * 2, DATA);
    /* addr 0 evicted from L1D, written back to L2 */

    /* Now force eviction from L2 (4-way) */
    uint64_t stride_l2 = 0x80000;
    /* addr 0x0, stride_l1d, stride_l1d*2 are in L2. Fill remaining ways and overflow. */
    /* Need enough addresses mapping to same L2 set to evict addr 0. */
    /* L2 set for addr 0: index = 0. stride_l2 keeps same L2 index. */
    for (int i = 1; i <= 4; i++) {
        read_cache(stride_l2 * i, DATA);
    }

    /* Now addr 0 should have been evicted from L2 and written to memory */
    ASSERT_EQ(memory[0], 0xDD);
}

/* Test 11: Hidden-trace regression - dirty L1D child survives as clean on L2 backinvalidate */
TEST(test_l2_backinvalidate_dirty_l1_regression) {
    clear_memory();
    init_cache(LRU);

    /* 0x180000 shares an L2 set with 0x0, 0x80000, 0x100000, 0x200000 */
    write_cache(0x180000, 0xAB, DATA);
    read_cache(0x000000, INSTR);
    read_cache(0x080000, INSTR);
    read_cache(0x100000, INSTR);
    read_cache(0x200000, DATA);

    cache_line_t *line = get_l1_data_cache_line(0x180000);
    ASSERT_NOT_NULL(line);
    ASSERT_EQ(line->valid, 1);
    ASSERT_EQ(line->modified, 0);
    ASSERT_EQ(line->tag, 0x60);
}

/* Test 12: Hidden-trace regression - deterministic RANDOM keeps 0x4240 resident */
TEST(test_random_address_decode_multi_set_regression) {
    clear_memory();
    seed_memory();
    srand(1);
    init_cache(RANDOM);

    read_cache(0x004240, DATA);
    read_cache(0x084240, DATA);
    read_cache(0x104240, DATA);
    read_cache(0x184240, DATA);
    read_cache(0x004240, DATA);
    read_cache(0x084240, DATA);
    read_cache(0x080240, DATA);

    cache_line_t *line = get_l1_data_cache_line(0x004240);
    ASSERT_NOT_NULL(line);
    ASSERT_EQ(line->valid, 1);
    ASSERT_EQ(line->modified, 0);
    ASSERT_EQ(line->tag, 0x1);
}

/* Test 13: Hidden-trace regression - deterministic RANDOM keeps dirty 0x4000 line */
TEST(test_random_mixed_spam_regression) {
    clear_memory();
    seed_memory();
    srand(1);
    init_cache(RANDOM);

    read_cache(0x000000, DATA);
    write_cache(0x004000, 0x11, DATA);
    read_cache(0x008000, DATA);
    read_cache(0x00c000, DATA);

    cache_line_t *line = get_l1_data_cache_line(0x004000);
    ASSERT_NOT_NULL(line);
    ASSERT_EQ(line->valid, 1);
    ASSERT_EQ(line->modified, 1);
    ASSERT_EQ(line->tag, 0x1);
}

/* ================================================================
 * REPLACEMENT POLICY COMPARISON
 * ================================================================ */

typedef struct {
    uint64_t l1i_misses, l1d_misses, l2_misses;
    uint64_t l1i_accesses, l1d_accesses, l2_accesses;
} run_result_t;

static run_result_t collect_stats(void) {
    run_result_t r;
    cache_stats_t s;
    s = get_l1_instr_stats();
    r.l1i_accesses = s.accesses; r.l1i_misses = s.misses;
    s = get_l1_data_stats();
    r.l1d_accesses = s.accesses; r.l1d_misses = s.misses;
    s = get_l2_stats();
    r.l2_accesses = s.accesses; r.l2_misses = s.misses;
    return r;
}

static void print_results(const char *label, run_result_t r) {
    printf("    %-10s | L1I: %7llu / %-7llu miss | L1D: %7llu / %-7llu miss | L2: %7llu / %-7llu miss\n",
           label,
           (unsigned long long)r.l1i_misses, (unsigned long long)r.l1i_accesses,
           (unsigned long long)r.l1d_misses, (unsigned long long)r.l1d_accesses,
           (unsigned long long)r.l2_misses,  (unsigned long long)r.l2_accesses);
}

/* Workload A: Sequential scan — large array, stride 1 byte */
static void workload_sequential(void) {
    for (uint64_t addr = 0; addr < 256 * 1024; addr++) {
        read_cache(addr, DATA);
    }
}

/* Workload B: Strided access — stride 128 (every other cache line) */
static void workload_strided(void) {
    for (uint64_t i = 0; i < 50000; i++) {
        uint64_t addr = (i * 128) % (4 * 1024 * 1024);
        read_cache(addr, DATA);
    }
}

/* Workload C: Repeated hot set — small working set that fits in L1 */
static void workload_hot_set(void) {
    for (int rep = 0; rep < 1000; rep++) {
        for (uint64_t addr = 0; addr < 8 * 1024; addr += 64) {
            read_cache(addr, DATA);
        }
    }
}

/* Workload D: Thrashing — working set slightly larger than L1D */
static void workload_thrash_l1(void) {
    /* L1D = 32KB. Access 40KB worth of data repeatedly,
       mapping to the same sets to cause conflict misses. */
    for (int rep = 0; rep < 100; rep++) {
        for (uint64_t i = 0; i < 640; i++) {
            /* 3 addresses per L1D set (2-way) -> guaranteed evictions */
            uint64_t set = i % 256;
            uint64_t way_offset = (i / 256) * 0x4000;
            uint64_t addr = set * 64 + way_offset;
            read_cache(addr, DATA);
        }
    }
}

/* Workload E: Mixed instruction + data */
static void workload_mixed(void) {
    for (uint64_t i = 0; i < 100000; i++) {
        uint64_t iaddr = (i * 4) % (64 * 1024);       /* instruction fetches */
        uint64_t daddr = 0x100000 + (i * 8) % (128 * 1024); /* data accesses */
        read_cache(iaddr, INSTR);
        read_cache(daddr, DATA);
        if (i % 4 == 0)
            write_cache(daddr, (uint8_t)i, DATA);
    }
}

/* Workload F: Random access — pseudorandom addresses */
static void workload_random_access(void) {
    uint64_t state = 12345;
    for (int i = 0; i < 100000; i++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        uint64_t addr = (state >> 16) % (8 * 1024 * 1024);
        read_cache(addr, DATA);
    }
}

typedef void (*workload_fn)(void);

static void run_comparison(const char *name, workload_fn workload) {
    printf("\n  --- %s ---\n", name);

    clear_memory();
    seed_memory();
    srand(42);
    init_cache(LRU);
    workload();
    run_result_t lru = collect_stats();
    print_results("LRU", lru);

    clear_memory();
    seed_memory();
    srand(42);
    init_cache(RANDOM);
    workload();
    run_result_t rnd = collect_stats();
    print_results("RANDOM", rnd);

    /* Show delta */
    long long l1d_delta = (long long)rnd.l1d_misses - (long long)lru.l1d_misses;
    long long l2_delta  = (long long)rnd.l2_misses  - (long long)lru.l2_misses;
    printf("    Delta (RND-LRU): L1D misses %+lld, L2 misses %+lld\n",
           l1d_delta, l2_delta);
}

/* ================================================================
 * Main
 * ================================================================ */
int main(int argc, char *argv[]) {

    printf("========== UNIT TESTS ==========\n");
    RUN(test_basic_read);
    RUN(test_read_hit);
    RUN(test_basic_write);
    RUN(test_writeback_on_eviction);
    RUN(test_instr_cache);
    RUN(test_l1_coherency);
    RUN(test_same_block_different_offsets);
    RUN(test_l2_inclusive_eviction);
    RUN(test_lru_order);
    RUN(test_writeback_reaches_memory);
    RUN(test_l2_backinvalidate_dirty_l1_regression);
    RUN(test_random_address_decode_multi_set_regression);
    RUN(test_random_mixed_spam_regression);
    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);

    printf("\n========== REPLACEMENT POLICY COMPARISON ==========\n");
    run_comparison("Sequential scan (256KB)", workload_sequential);
    run_comparison("Strided access (stride 128)", workload_strided);
    run_comparison("Hot set (8KB fits in L1)", workload_hot_set);
    run_comparison("L1 thrashing (conflict misses)", workload_thrash_l1);
    run_comparison("Mixed instr+data", workload_mixed);
    run_comparison("Random access pattern", workload_random_access);

    printf("\n========== DONE ==========\n");
    return tests_failed > 0 ? 1 : 0;
}
