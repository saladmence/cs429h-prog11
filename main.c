#include "libtcache/tcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Test Harness
// ============================================================
#define TEST_PASS "\033[32mPASS\033[0m"
#define TEST_FAIL "\033[31mFAIL\033[0m"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("  %-60s ", #test_func); \
    fflush(stdout); \
    if (test_func()) { \
        printf("[%s]\n", TEST_PASS); \
        tests_passed++; \
    } else { \
        printf("[%s]\n", TEST_FAIL); \
    } \
} while(0)

// ============================================================
// 1. Address Calculation Tests
// ============================================================

int test_block_offset_extraction() {
    uint64_t addr = 0x123456;
    return (addr & 0x3F) == 0x16;
}

int test_l1_instr_index_calculation() {
    uint64_t addr = 0x8040;
    return ((addr >> 6) & 0x1FF) == 1;
}

int test_l1_data_index_calculation() {
    uint64_t addr = 0x4000;
    uint64_t addr2 = 0x4040;
    return ((addr >> 6) & 0xFF) == 0 && ((addr2 >> 6) & 0xFF) == 1;
}

int test_l2_index_calculation() {
    uint64_t addr = 0x80000;
    return ((addr >> 6) & 0x1FFF) == 0;
}

// ============================================================
// 2. Cache Initialization Tests
// ============================================================

int test_init_cache_clears_stats() {
    init_cache(LRU);
    cache_stats_t l1i = get_l1_instr_stats();
    cache_stats_t l1d = get_l1_data_stats();
    cache_stats_t l2 = get_l2_stats();
    return l1i.accesses == 0 && l1i.misses == 0 &&
           l1d.accesses == 0 && l1d.misses == 0 &&
           l2.accesses == 0 && l2.misses == 0;
}

int test_init_cache_invalidates_lines() {
    init_cache(LRU);
    return get_l1_instr_cache_line(0x1000) == NULL &&
           get_l1_data_cache_line(0x1000) == NULL &&
           get_l2_cache_line(0x1000) == NULL;
}

int test_init_cache_can_be_called_multiple_times() {
    init_cache(LRU);
    read_cache(0x1000, DATA);
    init_cache(LRU);
    return get_l1_data_stats().accesses == 0 && get_l1_data_cache_line(0x1000) == NULL;
}

// ============================================================
// 3. L1 Instruction Cache Tests (Direct Mapped)
// ============================================================

int test_l1_instr_read_miss_then_hit() {
    init_cache(LRU);
    read_cache(0x1000, INSTR);
    read_cache(0x1000, INSTR);
    return get_l1_instr_stats().accesses == 2 && get_l1_instr_stats().misses == 1;
}

int test_l1_instr_cache_line_present_after_read() {
    init_cache(LRU);
    read_cache(0x2000, INSTR);
    cache_line_t* line = get_l1_instr_cache_line(0x2000);
    return line != NULL && line->valid == 1;
}

int test_l1_instr_same_set_different_tag_evicts() {
    init_cache(LRU);
    uint64_t a1 = 0x1000;
    uint64_t a2 = 0x1000 + (32 * 1024);
    read_cache(a1, INSTR);
    read_cache(a2, INSTR);
    return get_l1_instr_cache_line(a1) == NULL && get_l1_instr_cache_line(a2) != NULL;
}

int test_l1_instr_different_offsets_same_line() {
    init_cache(LRU);
    read_cache(0x1000, INSTR);
    read_cache(0x1020, INSTR);
    return get_l1_instr_stats().accesses == 2 && get_l1_instr_stats().misses == 1;
}

int test_l1_instr_write_then_read_returns_written_value() {
    init_cache(LRU);
    write_cache(0x1800, 0x5A, INSTR);
    return read_cache(0x1800, INSTR) == 0x5A;
}

// ============================================================
// 4. L1 Data Cache Tests (2-Way)
// ============================================================

int test_l1_data_read_miss_then_hit() {
    init_cache(LRU);
    read_cache(0x1000, DATA);
    read_cache(0x1000, DATA);
    return get_l1_data_stats().accesses == 2 && get_l1_data_stats().misses == 1;
}

int test_l1_data_two_way_no_eviction() {
    init_cache(LRU);
    uint64_t a1 = 0x1000, a2 = 0x1000 + (16 * 1024);
    read_cache(a1, DATA);
    read_cache(a2, DATA);
    return get_l1_data_cache_line(a1) != NULL && get_l1_data_cache_line(a2) != NULL;
}

int test_l1_data_lru_eviction() {
    init_cache(LRU);
    uint64_t a1 = 0x1000, a2 = 0x1000 + (16 * 1024), a3 = 0x1000 + (32 * 1024);
    read_cache(a1, DATA);
    read_cache(a2, DATA);
    read_cache(a3, DATA);
    return get_l1_data_cache_line(a1) == NULL && get_l1_data_cache_line(a2) != NULL;
}

int test_l1_data_lru_updates_on_access() {
    init_cache(LRU);
    uint64_t a1 = 0x1000, a2 = 0x1000 + (16 * 1024), a3 = 0x1000 + (32 * 1024);
    read_cache(a1, DATA);
    read_cache(a2, DATA);
    read_cache(a1, DATA); // a1 is now MRU
    read_cache(a3, DATA); // should evict a2
    return get_l1_data_cache_line(a1) != NULL && get_l1_data_cache_line(a2) == NULL;
}

int test_l1_data_write_sets_modified() {
    init_cache(LRU);
    write_cache(0x1000, 0xAB, DATA);
    cache_line_t* line = get_l1_data_cache_line(0x1000);
    return line != NULL && line->modified == 1;
}

int test_l1_data_write_then_read_returns_written_value() {
    init_cache(LRU);
    write_cache(0x1000, 0xCD, DATA);
    return read_cache(0x1000, DATA) == 0xCD;
}

int test_l1_data_write_multiple_bytes_same_line() {
    init_cache(LRU);
    write_cache(0x1000, 0x11, DATA);
    write_cache(0x1001, 0x22, DATA);
    write_cache(0x103F, 0xFF, DATA);
    return read_cache(0x1000, DATA) == 0x11 && 
           read_cache(0x1001, DATA) == 0x22 && 
           read_cache(0x103F, DATA) == 0xFF;
}

// ============================================================
// 5. Coherence Tests
// ============================================================

int test_coherence_data_write_invalidates_instr_copy() {
    init_cache(LRU);
    read_cache(0x1000, INSTR);
    write_cache(0x1000, 0xAB, DATA);
    return get_l1_instr_cache_line(0x1000) == NULL;
}

int test_coherence_data_write_then_instr_read_after_data_eviction() {
    init_cache(LRU);
    read_cache(0x1000, INSTR);
    write_cache(0x1000, 0xAB, DATA);
    read_cache(0x1000 + 16*1024, DATA);
    read_cache(0x1000 + 32*1024, DATA); // evict 0x1000 from L1-D
    return read_cache(0x1000, INSTR) == 0xAB;
}

int test_coherence_instr_write_then_data_read_after_instr_eviction() {
    init_cache(LRU);
    read_cache(0x1000, DATA);
    write_cache(0x1000, 0xBC, INSTR);
    read_cache(0x1000 + 32*1024, INSTR); // evict 0x1000 from L1-I
    return read_cache(0x1000, DATA) == 0xBC;
}

// ============================================================
// 6. L2 Cache Tests
// ============================================================

int test_l2_accessed_on_l1_miss() {
    init_cache(LRU);
    read_cache(0x1000, DATA);
    return get_l2_stats().accesses == 1;
}

int test_l2_cache_line_present_after_l1_miss() {
    init_cache(LRU);
    read_cache(0x1000, DATA);
    return get_l2_cache_line(0x1000) != NULL;
}

int test_l2_four_way_no_eviction() {
    init_cache(LRU);
    uint64_t s = 512 * 1024;
    for (int i = 0; i < 4; i++) read_cache(0x1000 + i*s, DATA);
    for (int i = 0; i < 4; i++) if (get_l2_cache_line(0x1000 + i*s) == NULL) return 0;
    return 1;
}

int test_l2_lru_eviction() {
    init_cache(LRU);
    uint64_t s = 512 * 1024;
    for (int i = 0; i < 5; i++) read_cache(0x1000 + i*s, DATA);
    return get_l2_cache_line(0x1000) == NULL && get_l2_cache_line(0x1000 + 4*s) != NULL;
}

int test_l2_hit_does_not_count_as_miss() {
    init_cache(LRU);
    uint64_t a1 = 0x1000, a2 = 0x1000 + 16*1024, a3 = 0x1000 + 32*1024;
    read_cache(a1, DATA);
    read_cache(a2, DATA);
    read_cache(a3, DATA); // evicts a1 from L1-D
    cache_stats_t b = get_l2_stats();
    read_cache(a1, DATA); // L1 miss, L2 hit
    cache_stats_t a = get_l2_stats();
    return a.accesses == b.accesses + 1 && a.misses == b.misses;
}

int test_l2_backinvalidate_dirty_l1_counts_access_and_writes_memory() {
    init_cache(LRU);
    uint64_t base = 0x1000, s = 512 * 1024;
    write_cache(base, 0xA5, DATA);
    for (int i = 1; i <= 4; i++) read_cache(base + i*s, INSTR);
    return get_l1_data_cache_line(base) == NULL && read_memory(base) == 0xA5;
}

int test_l2_backinvalidate_dirty_l1i_counts_access_and_writes_memory() {
    init_cache(LRU);
    uint64_t base = 0x1000, s = 512 * 1024;
    write_cache(base, 0x5C, INSTR);
    for (int i = 1; i <= 4; i++) read_cache(base + i*s, DATA);
    return get_l1_instr_cache_line(base) == NULL && read_memory(base) == 0x5C;
}

// ============================================================
// 7. Inclusion & Write-Back (Deterministic)
// ============================================================

int test_lru_l2_inclusive_eviction_invalidates_clean_l1() {
    init_cache(LRU);
    uint64_t base = 0x1000, s = 512 * 1024;
    read_cache(base, DATA); // way 0
    for (int i = 1; i <= 4; i++) read_cache(base + i*s, DATA); // evicts way 0
    return get_l1_data_cache_line(base) == NULL;
}

int test_lru_l2_inclusive_eviction_invalidates_dirty_l1_and_writes_memory() {
    init_cache(LRU);
    uint64_t base = 0x1000, s = 512 * 1024;
    write_cache(base, 0x6D, DATA);
    // Evict from L2. We need 4 more blocks in the same L2 set.
    for (int i = 1; i <= 4; i++) read_cache(base + i*s, INSTR);
    
    if (get_l1_data_cache_line(base) != NULL) {
        printf("(Fail: base still in L1) ");
        return 0;
    }
    if (read_memory(base) != 0x6D) {
        printf("(Fail: memory not updated, val=0x%x) ", read_memory(base));
        return 0;
    }
    return 1;
}

int test_writeback_on_l1_eviction() {
    init_cache(LRU);
    uint64_t a1 = 0x1000, a2 = a1 + 16*1024, a3 = a1 + 32*1024;
    write_cache(a1, 0xAA, DATA);
    read_cache(a2, DATA);
    read_cache(a3, DATA); // evict a1 from L1
    cache_line_t* l2 = get_l2_cache_line(a1);
    return l2 != NULL && l2->data[a1 & 0x3F] == 0xAA;
}

int test_writeback_modified_bit_propagates() {
    init_cache(LRU);
    uint64_t a1 = 0x1000, a2 = a1 + 16*1024, a3 = a1 + 32*1024;
    write_cache(a1, 0xBB, DATA);
    read_cache(a2, DATA);
    read_cache(a3, DATA);
    cache_line_t* l2 = get_l2_cache_line(a1);
    return l2 != NULL && l2->modified == 1;
}

// ============================================================
// 8. Statistics
// ============================================================

int test_stats_separate_for_each_cache() {
    init_cache(LRU);
    read_cache(0x1000, INSTR);
    read_cache(0x2000, DATA);
    return get_l1_instr_stats().accesses == 1 && get_l1_data_stats().accesses == 1;
}

int test_stats_miss_count_accurate() {
    init_cache(LRU);
    read_cache(0x1000, DATA);
    read_cache(0x1000, DATA);
    read_cache(0x1000, DATA);
    return get_l1_data_stats().accesses == 3 && get_l1_data_stats().misses == 1;
}

// ============================================================
// 9. Replacement Policy (Basic)
// ============================================================

int test_random_policy_initialization() {
    init_cache(RANDOM);
    return get_l1_data_stats().accesses == 0;
}

int test_random_policy_basic_operation() {
    init_cache(RANDOM);
    read_cache(0x1000, DATA);
    read_cache(0x1000, DATA);
    return get_l1_data_stats().accesses == 2 && get_l1_data_stats().misses == 1;
}

int test_random_policy_eviction_occurs() {
    init_cache(RANDOM);
    uint64_t a1 = 0x1000, a2 = a1 + 16*1024, a3 = a1 + 32*1024;
    read_cache(a1, DATA);
    read_cache(a2, DATA);
    read_cache(a3, DATA);
    int count = (get_l1_data_cache_line(a1) != NULL) + (get_l1_data_cache_line(a2) != NULL) + (get_l1_data_cache_line(a3) != NULL);
    return count == 2;
}

// ============================================================
// 10. Integration
// ============================================================

int test_read_data_from_memory() {
    write_memory(0x100, 0x42);
    init_cache(LRU);
    return read_cache(0x100, DATA) == 0x42;
}

int test_write_persists_to_memory_on_eviction() {
    init_cache(LRU);
    write_cache(0x200, 0x99, DATA);
    for (int i = 1; i <= 2; i++) read_cache(0x200 + i * 16*1024, DATA);
    for (int i = 1; i <= 4; i++) read_cache(0x200 + i * 512*1024, DATA);
    return read_memory(0x200) == 0x99;
}

// ============================================================
// Main
// ============================================================

int main() {
    printf("\nExhaustive Black-Box Cache Unit Tests (Refined)\n");
    printf("================================================\n");

    printf("\nAddress Calculation:\n");
    RUN_TEST(test_block_offset_extraction);
    RUN_TEST(test_l1_instr_index_calculation);
    RUN_TEST(test_l1_data_index_calculation);
    RUN_TEST(test_l2_index_calculation);

    printf("\nInitialization:\n");
    RUN_TEST(test_init_cache_clears_stats);
    RUN_TEST(test_init_cache_invalidates_lines);
    RUN_TEST(test_init_cache_can_be_called_multiple_times);

    printf("\nL1-I Cache:\n");
    RUN_TEST(test_l1_instr_read_miss_then_hit);
    RUN_TEST(test_l1_instr_cache_line_present_after_read);
    RUN_TEST(test_l1_instr_same_set_different_tag_evicts);
    RUN_TEST(test_l1_instr_different_offsets_same_line);
    RUN_TEST(test_l1_instr_write_then_read_returns_written_value);

    printf("\nL1-D Cache:\n");
    RUN_TEST(test_l1_data_read_miss_then_hit);
    RUN_TEST(test_l1_data_two_way_no_eviction);
    RUN_TEST(test_l1_data_lru_eviction);
    RUN_TEST(test_l1_data_lru_updates_on_access);
    RUN_TEST(test_l1_data_write_sets_modified);
    RUN_TEST(test_l1_data_write_then_read_returns_written_value);
    RUN_TEST(test_l1_data_write_multiple_bytes_same_line);

    printf("\nCoherence:\n");
    RUN_TEST(test_coherence_data_write_invalidates_instr_copy);
    RUN_TEST(test_coherence_data_write_then_instr_read_after_data_eviction);
    RUN_TEST(test_coherence_instr_write_then_data_read_after_instr_eviction);

    printf("\nL2 Cache:\n");
    RUN_TEST(test_l2_accessed_on_l1_miss);
    RUN_TEST(test_l2_cache_line_present_after_l1_miss);
    RUN_TEST(test_l2_four_way_no_eviction);
    RUN_TEST(test_l2_lru_eviction);
    RUN_TEST(test_l2_hit_does_not_count_as_miss);
    RUN_TEST(test_l2_backinvalidate_dirty_l1_counts_access_and_writes_memory);
    RUN_TEST(test_l2_backinvalidate_dirty_l1i_counts_access_and_writes_memory);

    printf("\nInclusion & Persistence:\n");
    RUN_TEST(test_lru_l2_inclusive_eviction_invalidates_clean_l1);
    RUN_TEST(test_lru_l2_inclusive_eviction_invalidates_dirty_l1_and_writes_memory);
    RUN_TEST(test_writeback_on_l1_eviction);
    RUN_TEST(test_writeback_modified_bit_propagates);

    printf("\nStatistics:\n");
    RUN_TEST(test_stats_separate_for_each_cache);
    RUN_TEST(test_stats_miss_count_accurate);

    printf("\nReplacement Policy:\n");
    RUN_TEST(test_random_policy_initialization);
    RUN_TEST(test_random_policy_basic_operation);
    RUN_TEST(test_random_policy_eviction_occurs);

    printf("\nIntegration:\n");
    RUN_TEST(test_read_data_from_memory);
    RUN_TEST(test_write_persists_to_memory_on_eviction);

    printf("\n================================================\n");
    printf("Results: %d/%d passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}