#include "tcache.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define LINE_SIZE 64
#define OFFSET 6

#define L1I_SETS 512 // HW11_L1_SIZE / LINE_SIZE / HW11_L1_INSTR_ASSOC
#define L1I_INDEX 9

#define L1D_SETS 256
#define L1D_INDEX 8

#define L2_SETS 8192
#define L2_INDEX 13

typedef struct cache_metadata {
    uint64_t tag;
    uint32_t lru;
} cache_metadata;

static cache_line_t l1i_lines[L1I_SETS*HW11_L1_INSTR_ASSOC];
static cache_metadata l1i_meta[L1I_SETS*HW11_L1_INSTR_ASSOC];

static cache_line_t l1d_lines[L1D_SETS*HW11_L1_DATA_ASSOC];
static cache_metadata l1d_meta[L1D_SETS*HW11_L1_DATA_ASSOC];

static cache_line_t l2_lines[L2_SETS*HW11_L2_ASSOC];
static cache_metadata l2_meta[L2_SETS*HW11_L2_ASSOC];

static cache_stats_t l1i_stats;
static cache_stats_t l1d_stats;
static cache_stats_t l2_stats;

static replacement_policy_e global_policy;

static cache_line_t *l2_access(uint64_t addr, bool write_back);
static void update_lru(cache_metadata *set_meta, int ways, int start);

uint64_t get_index(uint64_t addr, int index) {
    //uint64_t offset = addr & ((1ULL << OFFSET) - 1);
    return (addr >> OFFSET) & (((1ULL << index)) - 1);
}

uint64_t get_tag(uint64_t addr, int index) {
    return (addr >> (OFFSET + index));
}

uint64_t reconstruct_addr(uint64_t tag, uint64_t index, int idx) {
    return (tag << (OFFSET + idx)) | (index << OFFSET);
}

static void invalidate_line(cache_line_t *lines, cache_metadata *meta, int ways, int index_bits, uint64_t addr) {
    uint64_t index = get_index(addr, index_bits);
    uint64_t tag = get_tag(addr, index_bits);
    cache_line_t *set_lines = &lines[index * ways];
    cache_metadata *set_meta = &meta[index * ways];

    for (int w = 0; w < ways; w++) {
        if (set_lines[w].valid && set_meta[w].tag == tag) {
            set_lines[w].valid = 0;
            set_lines[w].modified = 0;
            set_lines[w].tag = 0;
            set_meta[w].tag = 0;
            return;
        }
    }
}

static cache_line_t *find_line(cache_line_t *lines, cache_metadata *meta, int ways, int index_bits, uint64_t addr) {
    uint64_t index = get_index(addr, index_bits);
    uint64_t tag = get_tag(addr, index_bits);
    cache_line_t *set_lines = &lines[index * ways];
    cache_metadata *set_meta = &meta[index * ways];

    for (int w = 0; w < ways; w++) {
        if (set_lines[w].valid && set_meta[w].tag == tag) {
            return &set_lines[w];
        }
    }

    return NULL;
}

static void invalidate_peer_l1_copy(mem_type_t source_type, uint64_t addr) {
    if (source_type == INSTR) {
        invalidate_line(l1d_lines, l1d_meta, HW11_L1_DATA_ASSOC, L1D_INDEX, addr);
    } else {
        invalidate_line(l1i_lines, l1i_meta, HW11_L1_INSTR_ASSOC, L1I_INDEX, addr);
    }
}

static void write_back_l1_line_to_l2(cache_line_t *line, uint64_t addr, mem_type_t source_type) {
    cache_line_t *l2_line = l2_access(addr, true);
    memcpy(l2_line->data, line->data, LINE_SIZE);
    l2_line->modified = 1;
}

typedef struct {
    bool had_dirty_child;
} l2_child_result_t;

static l2_child_result_t write_back_dirty_l1_children_to_l2(cache_line_t *set_lines, int victim, uint64_t victim_addr) {
    l2_child_result_t result = { false };

    cache_line_t *l1d_line = find_line(l1d_lines, l1d_meta, HW11_L1_DATA_ASSOC, L1D_INDEX, victim_addr);
    if (l1d_line != NULL && l1d_line->modified) {
        l2_stats.accesses++;
        memcpy(set_lines[victim].data, l1d_line->data, LINE_SIZE);
        set_lines[victim].modified = 1;
        invalidate_line(l1d_lines, l1d_meta, HW11_L1_DATA_ASSOC, L1D_INDEX, victim_addr);
        result.had_dirty_child = true;
    }

    cache_line_t *l1i_line = find_line(l1i_lines, l1i_meta, HW11_L1_INSTR_ASSOC, L1I_INDEX, victim_addr);
    if (l1i_line != NULL && l1i_line->modified) {
        l2_stats.accesses++;
        memcpy(set_lines[victim].data, l1i_line->data, LINE_SIZE);
        set_lines[victim].modified = 1;
        invalidate_line(l1i_lines, l1i_meta, HW11_L1_INSTR_ASSOC, L1I_INDEX, victim_addr);
        result.had_dirty_child = true;
    }

    return result;
}

static void maintain_l1_coherence(uint64_t addr, mem_type_t type) {
    cache_line_t *target_lines = (type == INSTR) ? l1i_lines : l1d_lines;
    cache_metadata *target_meta = (type == INSTR) ? l1i_meta : l1d_meta;
    int target_ways = (type == INSTR) ? HW11_L1_INSTR_ASSOC : HW11_L1_DATA_ASSOC;
    int target_index_bits = (type == INSTR) ? L1I_INDEX : L1D_INDEX;

    cache_line_t *peer_lines = (type == INSTR) ? l1d_lines : l1i_lines;
    cache_metadata *peer_meta = (type == INSTR) ? l1d_meta : l1i_meta;
    int peer_ways = (type == INSTR) ? HW11_L1_DATA_ASSOC : HW11_L1_INSTR_ASSOC;
    int peer_index_bits = (type == INSTR) ? L1D_INDEX : L1I_INDEX;

    cache_line_t *peer_line = find_line(peer_lines, peer_meta, peer_ways, peer_index_bits, addr);
    if (peer_line == NULL) {
        return;
    }

    if (peer_line->modified) {
        write_back_l1_line_to_l2(peer_line, addr, (type == INSTR) ? DATA : INSTR);
        peer_line->modified = 0;
        invalidate_line(target_lines, target_meta, target_ways, target_index_bits, addr);
    }

}

static void enforce_inclusive_l2_eviction(uint64_t victim_addr) {
    invalidate_line(l1d_lines, l1d_meta, HW11_L1_DATA_ASSOC, L1D_INDEX, victim_addr);
    invalidate_line(l1i_lines, l1i_meta, HW11_L1_INSTR_ASSOC, L1I_INDEX, victim_addr);
}

static void update_lru(cache_metadata *set_meta, int ways, int start) {
    uint32_t old_counter = set_meta[start].lru;
    for (int w = 0; w < ways; w++) {
        if (set_meta[w].lru < old_counter) set_meta[w].lru++;
    }
    set_meta[start].lru = 0;  
}

int pick_victim(cache_line_t *set_lines, cache_metadata *set_meta, int ways) {
    int victim = 0;
    for (int i = 0; i < ways; i++) {
        if (!set_lines[i].valid) return i;
    }

    if (global_policy == LRU) {
        for (int w = 1; w < ways; w++) {
            if (set_meta[w].lru > set_meta[victim].lru) victim = w;
        }
    } else victim = rand() % ways;
    return victim;
}

// STUDENT TODO: initialize cache with replacement policy
void init_cache(replacement_policy_e policy) {
    global_policy = policy;
    // zero everything
    memset(l1i_lines, 0, sizeof(l1i_lines)); memset(l1i_meta, 0, sizeof(l1i_meta));
    memset(l1d_lines, 0, sizeof(l1d_lines)); memset(l1d_meta, 0, sizeof(l1d_meta));
    memset(l2_lines, 0, sizeof(l2_lines)); memset(l2_meta, 0, sizeof(l2_meta));
    l1i_stats = (cache_stats_t) {0, 0}; l1d_stats = (cache_stats_t) {0, 0}; l2_stats = (cache_stats_t) {0, 0};

    for (int set = 0; set < L1I_SETS; set++) {
        for (int way = 0; way < HW11_L1_INSTR_ASSOC; way++) {
            l1i_meta[set * HW11_L1_INSTR_ASSOC + way].lru = way;
        }
    }

    for (int set = 0; set < L1D_SETS; set++) {
        for (int way = 0; way < HW11_L1_DATA_ASSOC; way++) {
            l1d_meta[set * HW11_L1_DATA_ASSOC + way].lru = way;
        }
    }

    for (int set = 0; set < L2_SETS; set++) {
        for (int way = 0; way < HW11_L2_ASSOC; way++) {
            l2_meta[set * HW11_L2_ASSOC + way].lru = way;
        }
    }
}

cache_line_t *l2_access(uint64_t addr, bool write_back) {
    l2_stats.accesses++;
    uint64_t index = get_index(addr, L2_INDEX);
    uint64_t tag = get_tag(addr, L2_INDEX);
    cache_line_t *set_lines = &l2_lines[index*HW11_L2_ASSOC];
    cache_metadata *set_meta = &l2_meta[index*HW11_L2_ASSOC];

    for (int w = 0; w < HW11_L2_ASSOC; w++) { // checking for hits
        if (set_lines[w].valid && set_meta[w].tag == tag) {
            update_lru(set_meta, HW11_L2_ASSOC, w);
            return &set_lines[w];
        }
    }

    l2_stats.misses++;
    int victim = pick_victim(set_lines, set_meta, HW11_L2_ASSOC);

    if (set_lines[victim].valid) {
        uint64_t victim_addr = reconstruct_addr(set_meta[victim].tag, index, L2_INDEX);
        write_back_dirty_l1_children_to_l2(set_lines, victim, victim_addr);
    }

    if (set_lines[victim].valid) {
        uint64_t writeback = reconstruct_addr(set_meta[victim].tag, index, L2_INDEX);
        enforce_inclusive_l2_eviction(writeback);
    }

    if (set_lines[victim].valid && set_lines[victim].modified) {
        uint64_t writeback = reconstruct_addr(set_meta[victim].tag, index, L2_INDEX);
        for (int b = 0; b < LINE_SIZE; b++) { // dirty eviction
            write_memory(writeback + b, set_lines[victim].data[b]);
        }
    }

    if (!write_back) {
        uint64_t line_base = addr & ~((uint64_t) (LINE_SIZE - 1));
        for (int b = 0; b < LINE_SIZE; b++) {
            set_lines[victim].data[b] = read_memory(line_base + b);
        }
    }

    set_lines[victim].valid = 1;
    set_lines[victim].modified = 0;
    set_lines[victim].tag = tag;
    set_meta[victim].tag = tag;
    update_lru(set_meta, HW11_L2_ASSOC, victim);
    return &set_lines[victim];
}

cache_line_t* l1_access(cache_line_t* lines, cache_metadata *meta, cache_stats_t *stats, int ways, int index_bits, uint64_t addr, bool write, mem_type_t type) {
    stats->accesses++;
    uint64_t index = get_index(addr, index_bits);
    uint64_t tag = get_tag(addr, index_bits);
    cache_line_t *set_lines = &lines[index*ways];
    cache_metadata *set_meta = &meta[index*ways];

    for (int w = 0; w < ways; w++) { // checking for hits
        if (set_lines[w].valid && set_meta[w].tag == tag) {
            if (write) set_lines[w].modified = 1;
            update_lru(set_meta, ways, w);
            return &set_lines[w];
        }
    }

    stats->misses++;

    // 1. Pick victim BEFORE accessing L2 to ensure rand() is consumed in the correct order
    int victim = pick_victim(set_lines, set_meta, ways);

    // 2. Write back the dirty victim if necessary
    if (set_lines[victim].valid && set_lines[victim].modified) { 
        uint64_t wb = reconstruct_addr(set_meta[victim].tag, index, index_bits);
        write_back_l1_line_to_l2(&set_lines[victim], wb, type);
    }

    // 3. Fetch from L2
    uint8_t fetched_data[LINE_SIZE];
    uint64_t line_base = addr & ~((uint64_t)(LINE_SIZE - 1));
    cache_line_t *l2_line = l2_access(line_base, 0);
    memcpy(fetched_data, l2_line->data, LINE_SIZE);

    // 4. Populate the fetched line into the chosen L1 victim slot
    memcpy(set_lines[victim].data, fetched_data, LINE_SIZE);
    set_lines[victim].valid = 1;
    set_lines[victim].modified = write ? 1 : 0;
    set_lines[victim].tag = tag;
    set_meta[victim].tag = tag;
    update_lru(set_meta, ways, victim);
    
    return &set_lines[victim];
}

// STUDENT TODO: implement read cache, using the l1 and l2 structure
uint8_t read_cache(uint64_t mem_addr, mem_type_t type) {
    cache_line_t *line;
    maintain_l1_coherence(mem_addr, type);
    if (type == INSTR) line = l1_access(l1i_lines, l1i_meta, &l1i_stats, HW11_L1_INSTR_ASSOC, L1I_INDEX, mem_addr, 0, INSTR);
    else line = l1_access(l1d_lines, l1d_meta, &l1d_stats, HW11_L1_DATA_ASSOC, L1D_INDEX, mem_addr, 0, DATA);
    return line->data[mem_addr & ((1ULL << OFFSET) - 1)];
}

// STUDENT TODO: implement write cache, using the l1 and l2 structure
void write_cache(uint64_t mem_addr, uint8_t value, mem_type_t type) {
    cache_line_t *line;
    maintain_l1_coherence(mem_addr, type);
    if (type == INSTR) line = l1_access(l1i_lines, l1i_meta, &l1i_stats, HW11_L1_INSTR_ASSOC, L1I_INDEX, mem_addr, 1, INSTR);
    else line = l1_access(l1d_lines, l1d_meta, &l1d_stats, HW11_L1_DATA_ASSOC, L1D_INDEX, mem_addr, 1, DATA);
    line->data[mem_addr & ((1ULL << OFFSET) - 1)] = value;
    invalidate_peer_l1_copy(type, mem_addr);
}

// STUDENT TODO: implement functions to get cache stats
cache_stats_t get_l1_instr_stats() {
    return l1i_stats;
}

cache_stats_t get_l1_data_stats() {
    return l1d_stats;
}

cache_stats_t get_l2_stats() {
    return l2_stats;
}

// STUDENT TODO: implement a function returning a pointer to a specific cache line for an address
//               or null if the line is not present in the cache
cache_line_t* get_l1_instr_cache_line(uint64_t mem_addr) {
    uint64_t index = get_index(mem_addr, L1I_INDEX);
    uint64_t tag = get_tag(mem_addr, L1I_INDEX);

    for (int i = 0; i < HW11_L1_INSTR_ASSOC; i++) {
        int slot = index * HW11_L1_INSTR_ASSOC + i;
        if (l1i_lines[slot].valid && l1i_meta[slot].tag == tag) return &l1i_lines[slot];
    }
    return NULL;
}

cache_line_t* get_l1_data_cache_line(uint64_t mem_addr) {
    uint64_t index = get_index(mem_addr, L1D_INDEX);
    uint64_t tag = get_tag(mem_addr, L1D_INDEX);

    for (int i = 0; i < HW11_L1_DATA_ASSOC; i++) {
        int slot = index * HW11_L1_DATA_ASSOC + i;
        if (l1d_lines[slot].valid && l1d_meta[slot].tag == tag) return &l1d_lines[slot];
    }
    return NULL;
}

cache_line_t* get_l2_cache_line(uint64_t mem_addr) {
    uint64_t index = get_index(mem_addr, L2_INDEX);
    uint64_t tag = get_tag(mem_addr, L2_INDEX);

    for (int i = 0; i < HW11_L2_ASSOC; i++) {
        int slot = index * HW11_L2_ASSOC + i;
        if (l2_lines[slot].valid && l2_meta[slot].tag == tag) return &l2_lines[slot];
    }
    return NULL;
}
