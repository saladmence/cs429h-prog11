# Prog11

Name: Annie Li 

EID: al62328

Compile: ./build.sh

Run: run the hw11 executable or the test executable for tests

## Replacement Policy Comparison

### Methodology

To compare LRU and RANDOM replacement policies, I ran identical workloads under each policy and measured L1 Data and L2 miss rates. The workload consisted of four access patterns designed to stress different cache behaviors:

1. **Sequential access** (64KB): Tests spatial locality and compulsory misses
2. **Strided access** (1000 accesses at 16KB stride): Creates conflicts in the 2-way L1D cache
3. **Large working set** (512KB, 3 passes): Exceeds cache capacity to measure replacement efficiency
4. **Temporal locality loop** (5 conflicting addresses, 100 iterations): Directly tests replacement policy effectiveness when the working set exceeds associativity

### Results

| Policy | L1D Accesses | L1D Misses | L1D Miss Rate | L2 Misses | L2 Miss Rate |
|--------|--------------|------------|---------------|-----------|--------------|
| LRU    | 91,612       | 27,100     | 29.58%        | 8,224     | 30.35%       |
| RANDOM | 91,612       | 26,875     | 29.34%        | 8,224     | 30.60%       |

### Analysis

Both policies exhibited nearly identical performance on this workload, with miss rates differing by less than 1%. This is expected because: (1) sequential and strided accesses are dominated by compulsory and capacity misses, which no replacement policy can avoid, and (2) the RANDOM policy, while theoretically suboptimal, performs reasonably well in practice when the working set significantly exceeds cache capacity. LRU's advantage becomes more pronounced with smaller working sets that fit within associativity bounds, where its recency tracking prevents thrashing.
