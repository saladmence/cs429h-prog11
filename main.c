#include "tcache.h"
#include <stdio.h>

#define PASS "\033[32mPASS\033[0m"
#define FAIL "\033[31mFAIL\033[0m"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("  %-52s ", #fn); \
    fflush(stdout); \
    if (fn()) { \
        tests_passed++; \
        printf("[%s]\n", PASS); \
    } else { \
        printf("[%s]\n", FAIL); \
    } \
} while (0)

static int test_basic_write_invalidates_l1i(void) {
    init_cache(LRU);

    read_cache(0x000000, INSTR);
    write_cache(0x000010, 0xAA, DATA);

    return get_l1_instr_cache_line(0x000000) == NULL;
}

static int test_coherence_l1d_to_l1i_write_invalidates_peer(void) {
    init_cache(LRU);

    read_cache(0x000001, INSTR);
    write_cache(0x000001, 0xA5, DATA);

    return get_l1_instr_cache_line(0x000000) == NULL;
}

static int test_coherence_l1i_to_l1d_write_invalidates_peer(void) {
    init_cache(LRU);

    read_cache(0x000041, DATA);
    write_cache(0x000041, 0x4D, INSTR);

    return get_l1_data_cache_line(0x000040) == NULL;
}

static int test_coherence_pingpong_write_invalidates_peer(void) {
    init_cache(LRU);

    read_cache(0x000401, DATA);
    write_cache(0x000401, 0x20, INSTR);

    return get_l1_data_cache_line(0x000400) == NULL;
}

static int test_mixed_spam_initial_write_invalidates_l1i(void) {
    init_cache(LRU);

    read_cache(0x000000, INSTR);
    write_cache(0x000010, 0x11, DATA);

    return get_l1_instr_cache_line(0x000000) == NULL;
}

static int test_l2_backinvalidate_dirty_l1_retouches_victim(void) {
    init_cache(LRU);

    write_cache(0x180000, 0xAB, DATA);
    read_cache(0x000000, INSTR);
    read_cache(0x080000, INSTR);
    read_cache(0x100000, INSTR);
    read_cache(0x200000, DATA);

    cache_line_t *line = get_l1_data_cache_line(0x180000);
    if (line == NULL) {
        return 0;
    }

    return line->valid == 1 && line->modified == 0 && line->tag == 0x60;
}

static int test_l2_backinvalidate_dirty_l1i_counts_extra_l2_access(void) {
    init_cache(LRU);

    write_cache(0x000000, 0x5C, INSTR);
    read_cache(0x080000, DATA);
    read_cache(0x100000, DATA);
    read_cache(0x180000, DATA);
    read_cache(0x200000, DATA);

    cache_stats_t l2 = get_l2_stats();
    cache_line_t *line = get_l1_instr_cache_line(0x000000);

    if (line == NULL) {
        return 0;
    }

    return l2.accesses == 6 && l2.misses == 5 &&
           line->valid == 1 && line->modified == 1 && line->tag == 0x0;
}

static int test_random_basic_write_invalidates_l1i(void) {
    init_cache(RANDOM);

    read_cache(0x000000, INSTR);
    write_cache(0x000010, 0xAA, DATA);

    return get_l1_instr_cache_line(0x000000) == NULL;
}

static int test_random_l1i_clean_line_survives_non_victim_inclusive_pressure(void) {
    init_cache(RANDOM);

    read_cache(0x080000, DATA);
    read_cache(0x100000, DATA);
    read_cache(0x000000, INSTR);
    read_cache(0x180000, DATA);
    read_cache(0x200000, DATA);

    return get_l1_instr_cache_line(0x000000) != NULL &&
           get_l2_cache_line(0x000000) != NULL;
}

int main(void) {
    printf("\n========================================\n");
    printf("Hidden Trace Regression Tests\n");
    printf("========================================\n\n");

    RUN_TEST(test_basic_write_invalidates_l1i);
    RUN_TEST(test_coherence_l1d_to_l1i_write_invalidates_peer);
    RUN_TEST(test_coherence_l1i_to_l1d_write_invalidates_peer);
    RUN_TEST(test_coherence_pingpong_write_invalidates_peer);
    RUN_TEST(test_mixed_spam_initial_write_invalidates_l1i);
    RUN_TEST(test_l2_backinvalidate_dirty_l1_retouches_victim);
    RUN_TEST(test_l2_backinvalidate_dirty_l1i_counts_extra_l2_access);
    RUN_TEST(test_random_basic_write_invalidates_l1i);
    RUN_TEST(test_random_l1i_clean_line_survives_non_victim_inclusive_pressure);

    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return tests_passed == tests_run ? 0 : 1;
}
