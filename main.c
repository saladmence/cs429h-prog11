#include "tcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LINE_SIZE 64
#define PASS printf("  [PASS] %s\n", __func__)
#define FAIL(msg) do { printf("  [FAIL] %s: %s\n", __func__, msg); return 0; } while(0)

extern uint8_t memory[HW11_MEM_SIZE];

static int tests_passed = 0;
static int tests_failed = 0;

void run_test(int (*test_fn)(void), const char* name) {
    printf("Running: %s\n", name);
    if (test_fn()) {
        tests_passed++;
    } else {
        tests_failed++;
    }
}

// =============================================================================
// TEST 1: Basic cold read - first access should miss
// =============================================================================
int test_cold_read_miss(void) {
    init_cache(LRU);
    memory[0x1000] = 0xAB;

    uint8_t val = read_cache(0x1000, DATA);
    cache_stats_t stats = get_l1_data_stats();

    if (val != 0xAB) FAIL("wrong value read");
    if (stats.accesses != 1) FAIL("expected 1 access");
    if (stats.misses != 1) FAIL("expected 1 miss on cold read");

    PASS; return 1;
}

// =============================================================================
// TEST 2: Read hit - second access to same address should hit
// =============================================================================
int test_read_hit(void) {
    init_cache(LRU);
    memory[0x2000] = 0xCD;

    read_cache(0x2000, DATA);  // cold miss
    read_cache(0x2000, DATA);  // should hit

    cache_stats_t stats = get_l1_data_stats();
    if (stats.accesses != 2) FAIL("expected 2 accesses");
    if (stats.misses != 1) FAIL("expected only 1 miss");

    PASS; return 1;
}

// =============================================================================
// TEST 3: Write then read - data integrity within cache
// =============================================================================
int test_write_then_read(void) {
    init_cache(LRU);

    write_cache(0x3000, 0x42, DATA);
    uint8_t val = read_cache(0x3000, DATA);

    if (val != 0x42) FAIL("read did not return written value");

    cache_stats_t stats = get_l1_data_stats();
    if (stats.accesses != 2) FAIL("expected 2 accesses");
    if (stats.misses != 1) FAIL("write should miss, read should hit");

    PASS; return 1;
}

// =============================================================================
// TEST 4: Cache line boundary - all 64 bytes in same line
// =============================================================================
int test_cache_line_boundary(void) {
    init_cache(LRU);
    uint64_t base = 0x4000;  // line-aligned address

    // Write to first and last byte of line
    write_cache(base, 0x11, DATA);
    write_cache(base + 63, 0x22, DATA);

    // Read both back
    uint8_t v1 = read_cache(base, DATA);
    uint8_t v2 = read_cache(base + 63, DATA);

    if (v1 != 0x11 || v2 != 0x22) FAIL("boundary bytes not preserved");

    cache_stats_t stats = get_l1_data_stats();
    // First write misses, rest hit same line
    if (stats.misses != 1) FAIL("all accesses should hit same line after first");

    PASS; return 1;
}

// =============================================================================
// TEST 5: Different cache lines - adjacent lines are separate
// =============================================================================
int test_different_lines(void) {
    init_cache(LRU);
    uint64_t line1 = 0x5000;
    uint64_t line2 = 0x5040;  // next line (64 bytes apart)

    write_cache(line1, 0xAA, DATA);
    write_cache(line2, 0xBB, DATA);

    cache_stats_t stats = get_l1_data_stats();
    if (stats.misses != 2) FAIL("different lines should each miss");

    // Verify data independence
    if (read_cache(line1, DATA) != 0xAA) FAIL("line1 corrupted");
    if (read_cache(line2, DATA) != 0xBB) FAIL("line2 corrupted");

    PASS; return 1;
}

// =============================================================================
// TEST 6: Instruction vs Data cache separation
// =============================================================================
int test_instr_data_separation(void) {
    init_cache(LRU);
    uint64_t addr = 0x6000;

    memory[addr] = 0x99;

    // Access same address as INSTR and DATA
    uint8_t instr_val = read_cache(addr, INSTR);
    uint8_t data_val = read_cache(addr, DATA);

    cache_stats_t i_stats = get_l1_instr_stats();
    cache_stats_t d_stats = get_l1_data_stats();

    if (i_stats.accesses != 1 || i_stats.misses != 1) FAIL("instr should have 1 access, 1 miss");
    if (d_stats.accesses != 1 || d_stats.misses != 1) FAIL("data should have 1 access, 1 miss");
    if (instr_val != 0x99 || data_val != 0x99) FAIL("values should match memory");

    PASS; return 1;
}

// =============================================================================
// TEST 7: get_cache_line returns correct pointer
// =============================================================================
int test_get_cache_line_present(void) {
    init_cache(LRU);
    uint64_t addr = 0x7000;

    // Before access, line should not be present
    if (get_l1_data_cache_line(addr) != NULL) FAIL("line should not exist before access");

    write_cache(addr, 0x77, DATA);

    cache_line_t* line = get_l1_data_cache_line(addr);
    if (line == NULL) FAIL("line should exist after access");
    if (!line->valid) FAIL("line should be valid");
    if (!line->modified) FAIL("line should be modified after write");
    if (line->data[0] != 0x77) FAIL("line data incorrect");

    PASS; return 1;
}

// =============================================================================
// TEST 8: L2 access on L1 miss
// =============================================================================
int test_l2_accessed_on_l1_miss(void) {
    init_cache(LRU);

    read_cache(0x8000, DATA);

    cache_stats_t l1_stats = get_l1_data_stats();
    cache_stats_t l2_stats = get_l2_stats();

    if (l1_stats.misses != 1) FAIL("L1 should miss");
    if (l2_stats.accesses < 1) FAIL("L2 should be accessed on L1 miss");

    PASS; return 1;
}

// =============================================================================
// TEST 9: L2 also populated on miss
// =============================================================================
int test_l2_populated(void) {
    init_cache(LRU);
    uint64_t addr = 0x9000;
    memory[addr] = 0x55;

    read_cache(addr, DATA);

    cache_line_t* l2_line = get_l2_cache_line(addr);
    if (l2_line == NULL) FAIL("L2 should have the line");
    if (l2_line->data[0] != 0x55) FAIL("L2 data incorrect");

    PASS; return 1;
}

// =============================================================================
// TEST 10: Modified bit set on write, clear on read
// =============================================================================
int test_modified_bit(void) {
    init_cache(LRU);
    uint64_t addr = 0xA000;

    // Read should not set modified
    memory[addr] = 0x33;
    read_cache(addr, DATA);
    cache_line_t* line = get_l1_data_cache_line(addr);
    if (line->modified) FAIL("read should not set modified bit");

    // Write should set modified
    init_cache(LRU);
    write_cache(addr, 0x44, DATA);
    line = get_l1_data_cache_line(addr);
    if (!line->modified) FAIL("write should set modified bit");

    PASS; return 1;
}

// =============================================================================
// TEST 11: LRU eviction in L1 Data (2-way)
// =============================================================================
int test_lru_eviction_l1d(void) {
    init_cache(LRU);

    // L1D: 256 sets, 2-way. Lines mapping to same set:
    // Set index = (addr >> 6) & 0xFF
    // Addresses with same set index but different tags will conflict
    // Set 0: addr 0, addr 256*64=0x4000, addr 512*64=0x8000, etc.

    uint64_t addr0 = 0x0000;        // set 0, way 0
    uint64_t addr1 = 0x4000;        // set 0, way 1 (256 sets * 64 bytes)
    uint64_t addr2 = 0x8000;        // set 0, will evict LRU

    memory[addr0] = 0x10;
    memory[addr1] = 0x20;
    memory[addr2] = 0x30;

    // Access addr0, then addr1
    read_cache(addr0, DATA);
    read_cache(addr1, DATA);

    // Both should be present
    if (get_l1_data_cache_line(addr0) == NULL) FAIL("addr0 should be present");
    if (get_l1_data_cache_line(addr1) == NULL) FAIL("addr1 should be present");

    // Access addr2 - should evict addr0 (LRU)
    read_cache(addr2, DATA);

    if (get_l1_data_cache_line(addr0) != NULL) FAIL("addr0 should be evicted (LRU)");
    if (get_l1_data_cache_line(addr1) == NULL) FAIL("addr1 should remain");
    if (get_l1_data_cache_line(addr2) == NULL) FAIL("addr2 should be present");

    PASS; return 1;
}

// =============================================================================
// TEST 12: LRU updates on access
// =============================================================================
int test_lru_update_on_access(void) {
    init_cache(LRU);

    uint64_t addr0 = 0x0000;
    uint64_t addr1 = 0x4000;
    uint64_t addr2 = 0x8000;

    // Access addr0, addr1
    read_cache(addr0, DATA);
    read_cache(addr1, DATA);

    // Re-access addr0 (now addr1 is LRU)
    read_cache(addr0, DATA);

    // Access addr2 - should evict addr1 (now LRU)
    read_cache(addr2, DATA);

    if (get_l1_data_cache_line(addr0) == NULL) FAIL("addr0 should remain (recently used)");
    if (get_l1_data_cache_line(addr1) != NULL) FAIL("addr1 should be evicted (LRU)");
    if (get_l1_data_cache_line(addr2) == NULL) FAIL("addr2 should be present");

    PASS; return 1;
}

// =============================================================================
// TEST 13: Write-back on eviction
// =============================================================================
int test_writeback_on_eviction(void) {
    init_cache(LRU);

    uint64_t addr0 = 0x0000;
    uint64_t addr1 = 0x4000;
    uint64_t addr2 = 0x8000;

    // Write to addr0 (modified)
    write_cache(addr0, 0xDE, DATA);

    // Fill set with addr1
    read_cache(addr1, DATA);

    // Evict addr0 by accessing addr2
    read_cache(addr2, DATA);

    // Check: addr0's data should be in L2 (written back)
    cache_line_t* l2_line = get_l2_cache_line(addr0);
    if (l2_line == NULL) FAIL("addr0 should be in L2 after eviction");
    if (l2_line->data[0] != 0xDE) FAIL("L2 should have written-back data");

    PASS; return 1;
}

// =============================================================================
// TEST 14: Direct-mapped L1 Instruction cache (1-way)
// =============================================================================
int test_l1i_direct_mapped(void) {
    init_cache(LRU);

    // L1I: 512 sets, 1-way (direct mapped)
    // Conflict distance: 512 * 64 = 0x8000
    uint64_t addr0 = 0x0000;
    uint64_t addr1 = 0x8000;  // same set, different tag

    memory[addr0] = 0xAA;
    memory[addr1] = 0xBB;

    read_cache(addr0, INSTR);
    if (get_l1_instr_cache_line(addr0) == NULL) FAIL("addr0 should be present");

    // Access addr1 - must evict addr0 (only 1 way)
    read_cache(addr1, INSTR);
    if (get_l1_instr_cache_line(addr0) != NULL) FAIL("addr0 should be evicted");
    if (get_l1_instr_cache_line(addr1) == NULL) FAIL("addr1 should be present");

    PASS; return 1;
}

// =============================================================================
// TEST 15: L2 4-way associativity
// =============================================================================
int test_l2_four_way(void) {
    init_cache(LRU);

    // L2: 8192 sets, 4-way
    // Conflict distance: 8192 * 64 = 0x80000
    uint64_t base = 0x0;
    uint64_t conflict = 0x80000;

    // Fill all 4 ways in L2 set 0
    for (int i = 0; i < 4; i++) {
        uint64_t addr = base + i * conflict;
        memory[addr] = 0x10 + i;
        read_cache(addr, DATA);
    }

    // All 4 should be in L2
    for (int i = 0; i < 4; i++) {
        uint64_t addr = base + i * conflict;
        if (get_l2_cache_line(addr) == NULL) FAIL("all 4 ways should be present");
    }

    // 5th access evicts one
    uint64_t addr4 = base + 4 * conflict;
    memory[addr4] = 0x50;
    read_cache(addr4, DATA);

    // addr0 should be evicted (LRU)
    if (get_l2_cache_line(base) != NULL) FAIL("LRU line should be evicted");
    if (get_l2_cache_line(addr4) == NULL) FAIL("new line should be present");

    PASS; return 1;
}

// =============================================================================
// TEST 16: Data survives eviction and reload
// =============================================================================
int test_data_survives_eviction(void) {
    init_cache(LRU);

    uint64_t addr0 = 0x0000;
    uint64_t addr1 = 0x4000;
    uint64_t addr2 = 0x8000;

    // Write to addr0
    write_cache(addr0, 0xAB, DATA);

    // Evict addr0 from L1
    read_cache(addr1, DATA);
    read_cache(addr2, DATA);

    // addr0 evicted from L1, but data in L2
    if (get_l1_data_cache_line(addr0) != NULL) FAIL("addr0 should be evicted from L1");

    // Re-read addr0 - should get correct value from L2
    uint8_t val = read_cache(addr0, DATA);
    if (val != 0xAB) FAIL("data should survive eviction");

    PASS; return 1;
}

// =============================================================================
// TEST 17: Memory writeback on L2 eviction
// =============================================================================
int test_l2_writeback_to_memory(void) {
    init_cache(LRU);

    // Need to evict from L2 to test memory writeback
    // L2: 8192 sets, 4-way. Conflict distance: 0x80000
    uint64_t base = 0x1000;
    uint64_t conflict = 0x80000;

    // Write to addr0, mark modified
    write_cache(base, 0xFE, DATA);

    // Force L1 eviction to get data to L2 with modified bit
    uint64_t l1_conflict1 = base + 0x4000;
    uint64_t l1_conflict2 = base + 0x8000;
    read_cache(l1_conflict1, DATA);
    read_cache(l1_conflict2, DATA);

    // Now L2 has the modified line. Fill L2 set to force eviction.
    for (int i = 1; i <= 4; i++) {
        read_cache(base + i * conflict, DATA);
    }

    // addr0 should be evicted from L2 and written to memory
    if (memory[base] != 0xFE) FAIL("data should be written to memory on L2 eviction");

    PASS; return 1;
}

// =============================================================================
// TEST 18: Multiple bytes in same line
// =============================================================================
int test_multiple_bytes_same_line(void) {
    init_cache(LRU);
    uint64_t base = 0xC000;

    // Write pattern across entire line
    for (int i = 0; i < 64; i++) {
        write_cache(base + i, (uint8_t)(i ^ 0xA5), DATA);
    }

    // Read back and verify
    for (int i = 0; i < 64; i++) {
        uint8_t val = read_cache(base + i, DATA);
        if (val != (uint8_t)(i ^ 0xA5)) FAIL("byte mismatch in line");
    }

    // Should be only 1 miss (first access)
    cache_stats_t stats = get_l1_data_stats();
    if (stats.misses != 1) FAIL("all accesses should hit same line");

    PASS; return 1;
}

// =============================================================================
// TEST 19: Stats reset on init
// =============================================================================
int test_stats_reset(void) {
    init_cache(LRU);
    read_cache(0x1000, DATA);
    read_cache(0x2000, DATA);

    cache_stats_t stats1 = get_l1_data_stats();
    if (stats1.accesses != 2) FAIL("should have 2 accesses");

    init_cache(LRU);
    cache_stats_t stats2 = get_l1_data_stats();
    if (stats2.accesses != 0 || stats2.misses != 0) FAIL("stats should reset on init");

    PASS; return 1;
}

// =============================================================================
// TEST 20: Clean eviction does not write back
// =============================================================================
int test_clean_eviction_no_writeback(void) {
    init_cache(LRU);

    uint64_t addr0 = 0x0000;
    uint64_t addr1 = 0x4000;
    uint64_t addr2 = 0x8000;

    // Set memory to known value
    memory[addr0] = 0x11;

    // Read (not write) - line is clean
    read_cache(addr0, DATA);

    // Modify memory directly (simulating external change)
    memory[addr0] = 0x99;

    // Evict addr0
    read_cache(addr1, DATA);
    read_cache(addr2, DATA);

    // Memory should still have 0x99 (no writeback of clean line)
    // Note: L2 has original value, but memory wasn't overwritten
    // Actually L2 also has 0x11 from initial load. This test checks
    // that clean L1 eviction doesn't corrupt L2.

    cache_line_t* l2_line = get_l2_cache_line(addr0);
    if (l2_line == NULL) FAIL("L2 should have addr0");
    if (l2_line->modified) FAIL("L2 line should be clean");

    PASS; return 1;
}

// =============================================================================
// TEST 21: Random replacement policy (basic sanity)
// =============================================================================
int test_random_policy(void) {
    init_cache(RANDOM);

    uint64_t addr0 = 0x0000;
    uint64_t addr1 = 0x4000;
    uint64_t addr2 = 0x8000;

    memory[addr0] = 0xAA;
    memory[addr1] = 0xBB;
    memory[addr2] = 0xCC;

    // Access all three
    read_cache(addr0, DATA);
    read_cache(addr1, DATA);
    read_cache(addr2, DATA);

    // One must be evicted (2-way cache)
    int present = 0;
    if (get_l1_data_cache_line(addr0) != NULL) present++;
    if (get_l1_data_cache_line(addr1) != NULL) present++;
    if (get_l1_data_cache_line(addr2) != NULL) present++;

    if (present != 2) FAIL("exactly 2 lines should be present in 2-way cache");

    // Data integrity still works
    uint8_t v0 = read_cache(addr0, DATA);
    uint8_t v1 = read_cache(addr1, DATA);
    uint8_t v2 = read_cache(addr2, DATA);

    if (v0 != 0xAA || v1 != 0xBB || v2 != 0xCC) FAIL("data integrity failed");

    PASS; return 1;
}

// =============================================================================
// TEST 22: Large address handling
// =============================================================================
int test_large_addresses(void) {
    init_cache(LRU);

    // Test near end of memory
    uint64_t addr = HW11_MEM_SIZE - 64;
    memory[addr] = 0xEE;

    write_cache(addr, 0xFF, DATA);
    uint8_t val = read_cache(addr, DATA);

    if (val != 0xFF) FAIL("large address read/write failed");

    PASS; return 1;
}

// =============================================================================
// TEST 23: Index and tag calculation
// =============================================================================
int test_index_tag_calculation(void) {
    init_cache(LRU);

    // Two addresses with same index but different tags
    // L1D: 256 sets, index bits = 8, offset bits = 6
    // Addresses differing only in tag bits should map to same set

    uint64_t addr1 = 0x00001000;  // index = (0x1000 >> 6) & 0xFF = 0x40
    uint64_t addr2 = 0x00005000;  // index = (0x5000 >> 6) & 0xFF = 0x40

    // Both map to set 0x40, should conflict
    memory[addr1] = 0x11;
    memory[addr2] = 0x22;

    read_cache(addr1, DATA);
    read_cache(addr2, DATA);

    // Both should be present (2-way)
    if (get_l1_data_cache_line(addr1) == NULL) FAIL("addr1 should be present");
    if (get_l1_data_cache_line(addr2) == NULL) FAIL("addr2 should be present");

    PASS; return 1;
}

// =============================================================================
// TEST 24: Write to instruction cache
// =============================================================================
int test_write_to_instr_cache(void) {
    init_cache(LRU);
    uint64_t addr = 0xD000;

    write_cache(addr, 0x77, INSTR);

    cache_line_t* line = get_l1_instr_cache_line(addr);
    if (line == NULL) FAIL("line should exist");
    if (!line->modified) FAIL("line should be modified");
    if (line->data[0] != 0x77) FAIL("data mismatch");

    PASS; return 1;
}

// =============================================================================
// TEST 25: Verify cache dimensions
// =============================================================================
int test_cache_dimensions(void) {
    init_cache(LRU);

    // L1I: 32KB, 1-way, 64B lines = 512 sets
    // Fill set 0 and set 511
    uint64_t l1i_set0 = 0x0000;
    uint64_t l1i_set511 = 511 * 64;  // 0x7FC0

    read_cache(l1i_set0, INSTR);
    read_cache(l1i_set511, INSTR);

    if (get_l1_instr_cache_line(l1i_set0) == NULL) FAIL("L1I set 0 failed");
    if (get_l1_instr_cache_line(l1i_set511) == NULL) FAIL("L1I set 511 failed");

    // L1D: 32KB, 2-way, 64B lines = 256 sets
    uint64_t l1d_set0 = 0x0000;
    uint64_t l1d_set255 = 255 * 64;  // 0x3FC0

    read_cache(l1d_set0, DATA);
    read_cache(l1d_set255, DATA);

    if (get_l1_data_cache_line(l1d_set0) == NULL) FAIL("L1D set 0 failed");
    if (get_l1_data_cache_line(l1d_set255) == NULL) FAIL("L1D set 255 failed");

    PASS; return 1;
}

// =============================================================================
// TEST 26: Sequential access pattern
// =============================================================================
int test_sequential_access(void) {
    init_cache(LRU);

    // Access 1KB sequentially (16 cache lines)
    uint64_t base = 0xE000;
    for (int i = 0; i < 1024; i++) {
        memory[base + i] = (uint8_t)i;
    }

    for (int i = 0; i < 1024; i++) {
        uint8_t val = read_cache(base + i, DATA);
        if (val != (uint8_t)i) FAIL("sequential data mismatch");
    }

    cache_stats_t stats = get_l1_data_stats();
    // 1024 bytes / 64 bytes per line = 16 misses
    if (stats.misses != 16) FAIL("expected 16 misses for 16 lines");
    if (stats.accesses != 1024) FAIL("expected 1024 accesses");

    PASS; return 1;
}

// =============================================================================
// TEST 27: L1 hit does not access L2
// =============================================================================
int test_l1_hit_no_l2_access(void) {
    init_cache(LRU);

    read_cache(0xF000, DATA);  // miss L1, access L2
    cache_stats_t l2_before = get_l2_stats();

    read_cache(0xF000, DATA);  // hit L1
    cache_stats_t l2_after = get_l2_stats();

    if (l2_after.accesses != l2_before.accesses) FAIL("L1 hit should not access L2");

    PASS; return 1;
}

// =============================================================================
// TEST 28: Offset extraction
// =============================================================================
int test_offset_extraction(void) {
    init_cache(LRU);
    uint64_t base = 0x10000;

    // Write different values at different offsets in same line
    for (int offset = 0; offset < 64; offset++) {
        write_cache(base + offset, (uint8_t)(offset + 1), DATA);
    }

    // Verify each offset
    for (int offset = 0; offset < 64; offset++) {
        uint8_t val = read_cache(base + offset, DATA);
        if (val != (uint8_t)(offset + 1)) FAIL("offset extraction error");
    }

    PASS; return 1;
}

// =============================================================================
// TEST 29: Writeback preserves all 64 bytes
// =============================================================================
int test_writeback_full_line(void) {
    init_cache(LRU);

    uint64_t addr0 = 0x0000;
    uint64_t addr1 = 0x4000;
    uint64_t addr2 = 0x8000;

    // Write entire line
    for (int i = 0; i < 64; i++) {
        write_cache(addr0 + i, (uint8_t)(0x80 + i), DATA);
    }

    // Evict to L2
    read_cache(addr1, DATA);
    read_cache(addr2, DATA);

    // Check L2 has all bytes
    cache_line_t* l2_line = get_l2_cache_line(addr0);
    if (l2_line == NULL) FAIL("L2 should have evicted line");

    for (int i = 0; i < 64; i++) {
        if (l2_line->data[i] != (uint8_t)(0x80 + i)) FAIL("L2 line data mismatch");
    }

    PASS; return 1;
}

// =============================================================================
// TEST 31: Coherence from L1D write to L1I read goes through L2
// =============================================================================
int test_coherence_l1d_to_l1i(void) {
    init_cache(LRU);
    uint64_t addr = 0x12000;

    memory[addr] = 0x10;

    // Populate instruction cache with the old value.
    if (read_cache(addr, INSTR) != 0x10) FAIL("initial instruction read mismatch");

    // Write a new value through L1D. The old L1I copy may remain until touched,
    // but the subsequent instruction read must refetch the coherent value via L2.
    write_cache(addr, 0x5A, DATA);

    // Reading as instruction should now observe the latest value via L2.
    if (read_cache(addr, INSTR) != 0x5A) FAIL("instruction read should observe coherent value");

    cache_line_t* l2_line = get_l2_cache_line(addr);
    if (l2_line == NULL) FAIL("L2 should contain the coherent line");
    if (l2_line->data[0] != 0x5A) FAIL("L2 should receive the written-back value");

    PASS; return 1;
}

// =============================================================================
// TEST 32: L2 inclusive eviction invalidates clean L1 copies
// =============================================================================
int test_l2_inclusive_eviction_invalidates_l1(void) {
    init_cache(LRU);

    uint64_t base = 0x2000;
    uint64_t conflict = 0x80000;

    read_cache(base, INSTR);

    for (int i = 1; i < 4; i++) {
        read_cache(base + i * conflict, DATA);
    }

    if (get_l1_instr_cache_line(base) == NULL) FAIL("base line should start in L1I");
    if (get_l2_cache_line(base) == NULL) FAIL("base line should start in L2");

    read_cache(base + 4 * conflict, DATA);

    if (get_l2_cache_line(base) != NULL) FAIL("base line should be evicted from L2");
    if (get_l1_instr_cache_line(base) != NULL) FAIL("inclusive L2 eviction should invalidate L1I");

    PASS; return 1;
}

// =============================================================================
// TEST 33: Dirty L1 copy is preserved on inclusive L2 eviction
// =============================================================================
int test_l2_inclusive_eviction_dirty_preserves_data(void) {
    init_cache(LRU);

    uint64_t base = 0x3000;
    uint64_t l2_conflict = 0x80000;

    write_cache(base, 0xD4, INSTR);

    for (int i = 1; i < 4; i++) {
        read_cache(base + i * l2_conflict, DATA);
    }

    if (get_l1_instr_cache_line(base) == NULL) FAIL("base line should still reside in L1I before L2 eviction");

    read_cache(base + 4 * l2_conflict, DATA);

    if (get_l2_cache_line(base) != NULL) FAIL("base line should be evicted from L2");
    if (get_l1_instr_cache_line(base) != NULL) FAIL("inclusive L2 eviction should invalidate dirty L1I line");
    if (memory[base] != 0xD4) FAIL("dirty data should be written to memory when L2 evicts");

    PASS; return 1;
}

// =============================================================================
// TEST 34: Exported cache line tag stays in sync
// =============================================================================
int test_cache_line_tag_field(void) {
    init_cache(LRU);

    uint64_t addr = 0x12340;

    read_cache(addr, DATA);

    cache_line_t* l1_line = get_l1_data_cache_line(addr);
    cache_line_t* l2_line = get_l2_cache_line(addr);

    if (l1_line == NULL || l2_line == NULL) FAIL("line should exist in both caches");
    if (l1_line->tag != (addr >> (6 + 8))) FAIL("L1D exported tag incorrect");
    if (l2_line->tag != (addr >> (6 + 13))) FAIL("L2 exported tag incorrect");

    PASS; return 1;
}

// =============================================================================
// TEST 35: Writing through stale peer must refetch newest line first
// =============================================================================
int test_write_through_stale_peer_refetches(void) {
    init_cache(LRU);

    uint64_t addr = 0x14000;

    memory[addr] = 0x11;
    memory[addr + 1] = 0x22;

    // Both L1 caches start with the same clean line.
    if (read_cache(addr, DATA) != 0x11) FAIL("initial data read mismatch");
    if (read_cache(addr, INSTR) != 0x11) FAIL("initial instr read mismatch");

    // Modify through L1I so L1D now holds a stale clean copy.
    write_cache(addr + 1, 0x99, INSTR);

    // A write through L1D must not merge into the stale cached bytes.
    write_cache(addr, 0x55, DATA);

    if (read_cache(addr, DATA) != 0x55) FAIL("written byte incorrect");
    if (read_cache(addr + 1, DATA) != 0x99) FAIL("peer-modified byte should be preserved");

    PASS; return 1;
}

// =============================================================================
// TEST 30: Stress test - many accesses
// =============================================================================
int test_stress(void) {
    init_cache(LRU);

    // Initialize memory with pattern
    for (uint64_t i = 0; i < 0x10000; i++) {
        memory[i] = (uint8_t)(i * 7 + 3);
    }

    // Random-ish access pattern
    for (int i = 0; i < 1000; i++) {
        uint64_t addr = (i * 137 + 42) % 0x10000;
        uint8_t expected = (uint8_t)(addr * 7 + 3);
        uint8_t actual = read_cache(addr, DATA);
        if (actual != expected) FAIL("stress test data mismatch");
    }

    // Write and read back
    for (int i = 0; i < 500; i++) {
        uint64_t addr = (i * 89 + 17) % 0x10000;
        write_cache(addr, (uint8_t)i, DATA);
    }

    for (int i = 0; i < 500; i++) {
        uint64_t addr = (i * 89 + 17) % 0x10000;
        uint8_t val = read_cache(addr, DATA);
        if (val != (uint8_t)i) FAIL("stress write/read mismatch");
    }

    PASS; return 1;
}

// =============================================================================
// MAIN
// =============================================================================
int main(int argc, char *argv[]) {
    printf("\n========================================\n");
    printf("    TINKER CACHE TEST SUITE\n");
    printf("========================================\n\n");

    // Basic operations
    run_test(test_cold_read_miss, "test_cold_read_miss");
    run_test(test_read_hit, "test_read_hit");
    run_test(test_write_then_read, "test_write_then_read");

    // Cache line boundaries
    run_test(test_cache_line_boundary, "test_cache_line_boundary");
    run_test(test_different_lines, "test_different_lines");
    run_test(test_multiple_bytes_same_line, "test_multiple_bytes_same_line");
    run_test(test_offset_extraction, "test_offset_extraction");

    // Instruction vs Data
    run_test(test_instr_data_separation, "test_instr_data_separation");
    run_test(test_write_to_instr_cache, "test_write_to_instr_cache");

    // Cache line lookup
    run_test(test_get_cache_line_present, "test_get_cache_line_present");

    // Modified bit
    run_test(test_modified_bit, "test_modified_bit");

    // L1/L2 interaction
    run_test(test_l2_accessed_on_l1_miss, "test_l2_accessed_on_l1_miss");
    run_test(test_l2_populated, "test_l2_populated");
    run_test(test_l1_hit_no_l2_access, "test_l1_hit_no_l2_access");

    // LRU eviction
    run_test(test_lru_eviction_l1d, "test_lru_eviction_l1d");
    run_test(test_lru_update_on_access, "test_lru_update_on_access");

    // Write-back
    run_test(test_writeback_on_eviction, "test_writeback_on_eviction");
    run_test(test_clean_eviction_no_writeback, "test_clean_eviction_no_writeback");
    run_test(test_l2_writeback_to_memory, "test_l2_writeback_to_memory");
    run_test(test_writeback_full_line, "test_writeback_full_line");
    run_test(test_coherence_l1d_to_l1i, "test_coherence_l1d_to_l1i");
    run_test(test_l2_inclusive_eviction_invalidates_l1, "test_l2_inclusive_eviction_invalidates_l1");
    run_test(test_l2_inclusive_eviction_dirty_preserves_data, "test_l2_inclusive_eviction_dirty_preserves_data");
    run_test(test_cache_line_tag_field, "test_cache_line_tag_field");
    run_test(test_write_through_stale_peer_refetches, "test_write_through_stale_peer_refetches");

    // Cache structure
    run_test(test_l1i_direct_mapped, "test_l1i_direct_mapped");
    run_test(test_l2_four_way, "test_l2_four_way");
    run_test(test_cache_dimensions, "test_cache_dimensions");
    run_test(test_index_tag_calculation, "test_index_tag_calculation");

    // Data integrity
    run_test(test_data_survives_eviction, "test_data_survives_eviction");
    run_test(test_sequential_access, "test_sequential_access");

    // Stats
    run_test(test_stats_reset, "test_stats_reset");

    // Random policy
    run_test(test_random_policy, "test_random_policy");

    // Edge cases
    run_test(test_large_addresses, "test_large_addresses");

    // Stress test
    run_test(test_stress, "test_stress");

    printf("\n========================================\n");
    printf("    RESULTS: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
