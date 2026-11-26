# Grid locking benchmark

`grid_lock_bench.cpp` re-creates the data structure from `src/grid.{h,cpp}`:
- each cell holds an `unordered_set<shared_ptr<Obj>>`;
- `Search` copies the shared_ptrs out of every cell that overlaps the query;
- an object that changes cell is removed and then re-inserted.

Only the lock layout changes between runs:

| Policy | Lock |
|---|---|
| `global-mutex` | one `std::mutex` for the whole grid |
| `global-rw` | one `std::shared_mutex` for the whole grid (searches take it shared) |
| `cell-mutex` | one `std::mutex` per cell |
| `cell-rw` | one `std::shared_mutex` per cell (what `src/grid.h` does) |

Workload, per thread, in a closed loop:
- **Move** (with probability `--write`): move one of the thread's own objects 10 units in a random direction. About 12% of moves cross a cell boundary and touch the grid.
- **Search** (otherwise): run the player view query, 2000×1300 (the view plus the margin from `constants.h`). Then collision-check the results outside the lock, as `HandleThreadClients` does.

Each data point is the median of 3 runs of 1 s each, after a 0.2 s warm-up.

```
make bench
./bin/grid_lock_bench examined
./bin/grid_lock_bench locks --world density      # world grows with n (constant density)
./bin/grid_lock_bench locks --world fixed        # the 1600x1600 world from main.cpp
```

The runs are race-free under ThreadSanitizer.

Machine: Apple M3 (4 performance + 4 efficiency cores), macOS, Apple clang 21, libc++.
- In libc++, `std::shared_mutex` is a `std::mutex` plus two condition variables. A shared lock still takes and releases that internal mutex.
- Runs vary by roughly ±30% depending on whether threads land on performance or efficiency cores.

## Objects examined per view query (constant density, 4.1e-5 objects per unit²)

| Objects | World side | Grid: examined | Grid: µs per query | Scan: examined | Scan: µs per query |
|---|---|---|---|---|---|
| 250 | 2,469 | 84 | 10.3 | 250 | 1.6 |
| 1,000 | 4,938 | 102 | 8.7 | 1,000 | 2.4 |
| 4,000 | 9,877 | 111 | 6.5 | 4,000 | 4.9 |
| 16,000 | 19,754 | 115 | 5.4 | 16,000 | 12.0 |
| 32,000 | 27,937 | 116 | 7.8 | 32,000 | 23.3 |

With the grid, the number examined stays at about 100–120, while a full scan grows linearly with the object count. Wall time is a different story: below about 5k objects the scan is faster. A query touches about 294 cells, and taking and releasing a lock on each one costs more than scanning a few thousand pointers.

In the fixed 1600×1600 world, a query covers about 80% of the grid and examines about 19,000 of 32,000 objects. The grid barely prunes anything there.

## Throughput at 4 threads, constant-density world, 32,000 objects (ops/s)

| Write fraction | global-mutex | global-rw | cell-mutex | cell-rw |
|---|---|---|---|---|
| 0 | 290k | 1.16M | 1.17M | 611k |
| 0.5 | 549k | 1.00M | 2.23M | 1.03M |
| 0.9 | 2.16M | 1.80M | 9.17M | 4.18M |

| Write fraction | cell-rw ÷ global-mutex | cell-rw ÷ global-rw | cell-mutex ÷ global-mutex |
|---|---|---|---|
| 0 | 2.1× | 0.53× | 4.0× |
| 0.5 | 1.9× | 1.03× | 4.1× |
| 0.9 | 1.9× | 2.3× | 4.2× |

`global-mutex` gets slower as threads are added: 1 → 4 threads gives 0.60× at write 0, 0.70× at write 0.5 and 0.52× at write 0.9. This is contention collapse. It's why per-cell vs global can beat the thread count.

## Throughput at 4 threads, fixed 1600 world, 32,000 objects (ops/s)

| Write fraction | global-mutex | global-rw | cell-mutex | cell-rw |
|---|---|---|---|---|
| 0 | 6.2k | 17.1k | 17.2k | 16.6k |
| 0.5 | 12.1k | 30.4k | 34.0k | 33.6k |
| 0.9 | 59k | 97k | 159k | 166k |

Here `cell-rw` ÷ `global-mutex` is 2.7–2.8×, and `cell-rw` ÷ `global-rw` is 0.97×, 1.10× and 1.72×.

## Takeaways

- No configuration reproduced 5.8× for `cell-rw` vs a global lock on this machine. The best was 2.8× (fixed world, 4 threads), and `cell-mutex` reached 4.2×.
- With reader-writer locks on both sides, per-cell locking wins only when writes are common. On read-only queries a single global `shared_mutex` is as fast or faster, because a query pays for one lock instead of about 294.
- Each query takes ~294 locks, so the fixed cost of each lock decides the result. libc++'s `shared_mutex` costs about 2–3× a plain mutex, so `cell-mutex` beats `cell-rw` in every row. Reader sharing only helps when readers actually collide, and with per-cell locks they rarely do.

## Reconciling with the earlier 5.8× measurement

An earlier session measured 5.83× for per-cell locks vs a global lock at 4 threads. Its benchmark (`bench_lock.cpp`, kept only in that session's scratchpad) used:
- 8,000 objects at 2.5e-5 per unit² (world side 17,889);
- **cell size 650**, so a query touches about 12 cells instead of about 294;
- **read-only queries**;
- a "global lock" that wrapped a `std::mutex` around the real `Grid::Search`, which still takes its per-cell locks inside.

The same configuration rerun here (`--n 8000 --density 2.5e-5 --cell 650`), 4 threads, ops/s:

| Write fraction | global-mutex | global-rw | cell-mutex | cell-rw | cell-rw ÷ global-mutex | cell-rw ÷ global-rw |
|---|---|---|---|---|---|---|
| 0 | 606k | 2.61M | 3.08M | 2.81M | 4.6× | 1.08× |
| 0.5 | 1.07M | 3.55M | 5.58M | 5.21M | 4.9× | 1.47× |
| 0.9 | 4.68M | 7.14M | 23.5M | 22.1M | 4.7× | 3.1× |

From 1 to 4 threads, `global-mutex` drops from 1.36M to 606k (0.44×). The earlier session saw 950k → 444k.

This reproduces the earlier result: 4.6–4.9× here, and 5.8× in a variant whose global lock also paid the inner per-cell locks. The result depends on the query touching few cells. At the production cell size of 100 the same comparison gives about 2×.

With reader-writer locks on both sides and read-only queries, per-cell and global are the same (1.08×). Most of the 5.8× comes from the global mutex making readers wait for each other. Per-cell granularity adds its own gain only once writes are common (1.5× at 50% writes, 3.1× at 90%).
