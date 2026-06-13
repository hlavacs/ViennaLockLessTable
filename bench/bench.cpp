#include <iostream>
#include <vector>
#include <thread>
#include <latch>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <iomanip>

#include "VLLT.h"

// ---------------------------------------------------------------------------
// TSC calibration (busy-spin to avoid Windows sleep inaccuracy)
// ---------------------------------------------------------------------------

static double g_ns_per_cycle = 1.0;

inline uint64_t rdtsc() noexcept { return __builtin_ia32_rdtsc(); }

static void calibrate_tsc() {
    using clk = std::chrono::high_resolution_clock;
    uint64_t c0 = rdtsc();
    auto t0 = clk::now();
    uint64_t x = 1;
    auto deadline = t0 + std::chrono::milliseconds(50);
    while (clk::now() < deadline) { x ^= x<<13; x ^= x>>7; x ^= x<<17; }
    (void)x;
    uint64_t c1 = rdtsc();
    auto t1 = clk::now();
    double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count();
    g_ns_per_cycle = ns / (double)(c1 - c0);
}

// ---------------------------------------------------------------------------
// Busy work: xorshift64, seed is live so the compiler cannot hoist the loop
// ---------------------------------------------------------------------------

inline uint64_t busy_work(uint64_t seed, uint32_t iters) noexcept {
    for (uint32_t i = 0; i < iters; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
    }
    return seed;
}

// ---------------------------------------------------------------------------
// Table type
// ---------------------------------------------------------------------------

using DATA = vtll::tl<uint64_t>;
static constexpr size_t TABLE_ROWS  = 64;
static constexpr size_t BLOCK_SIZE  = 1 << 6;  // 64

// View factory helpers
// Read-only: place VlltWrite at the END → READ={uint64_t}, WRITE={}
// Write-only: place VlltWrite at the START → READ={},        WRITE={uint64_t}
// Note: view<uint64_t>() is broken in current VLLT when VlltWrite is absent
// (vtll::sublist with count=SIZE_MAX returns empty list); explicit delimiter works.

// ---------------------------------------------------------------------------
// Per-thread result
// ---------------------------------------------------------------------------

struct BenchResult {
    uint64_t            checksum;
    std::vector<uint64_t> lat;   // per-op RDTSC deltas (cycles)
    uint64_t            wall;    // max-across-all-ops wall cycles
};

// ---------------------------------------------------------------------------
// Populate: single-threaded, called before threads launch
// ---------------------------------------------------------------------------

template<vllt::sync_t SYNC>
static void populate(vllt::VlltStaticTable<DATA, SYNC, BLOCK_SIZE>& table) {
    auto view = table.template view<>();   // full owner (WRITE=DATA), push_back OK
    for (size_t i = 0; i < TABLE_ROWS; ++i)
        view.push_back(uint64_t(i + 1));
}

// ---------------------------------------------------------------------------
// Worker: lockless mode (VLLT_SYNC_INTERNAL)
// ---------------------------------------------------------------------------

BenchResult run_lockless(
    vllt::VlltStaticTable<DATA, vllt::sync_t::VLLT_SYNC_INTERNAL, BLOCK_SIZE>& table,
    int      tid,
    uint64_t ops,
    uint32_t work_per_op,
    uint32_t write_pct,
    uint64_t warmup,
    std::latch& go,
    std::latch& done)
{
    // --- warmup ---
    for (uint64_t i = 0; i < warmup; ++i) {
        uint64_t s = uint64_t(tid + 1000) * warmup + i + 1;
        uint64_t bw = busy_work(s, work_per_op);
        uint32_t row = uint32_t(s % TABLE_ROWS);
        if ((i % 100) < write_pct) {
            auto v = table.template view<vllt::VlltWrite, uint64_t>();
            auto t = v.get_ref_tuple(vllt::table_index_t{row});
            std::get<0>(t) = bw & 0xFFFF;
        } else {
            auto v = table.template view<uint64_t, vllt::VlltWrite>();
            auto t = v.get_ref_tuple(vllt::table_index_t{row});
            bw ^= std::get<0>(t);
        }
        (void)bw;
    }

    go.arrive_and_wait();
    uint64_t wall0 = rdtsc();

    uint64_t checksum = 0;
    std::vector<uint64_t> lat(ops);

    for (uint64_t i = 0; i < ops; ++i) {
        uint64_t s   = uint64_t(tid) * ops + i + 1;  // +1: seed=0 → bw=0 (xorshift degeneracy)
        uint64_t bw  = busy_work(s, work_per_op);
        checksum    ^= bw;                           // deterministic, before table op
        uint32_t row = uint32_t(s % TABLE_ROWS);

        uint64_t t0 = rdtsc();
        if ((i % 100) < write_pct) {
            auto v  = table.template view<vllt::VlltWrite, uint64_t>();
            auto tp = v.get_ref_tuple(vllt::table_index_t{row});
            std::get<0>(tp) = bw & 0xFFFF;
        } else {
            auto v  = table.template view<uint64_t, vllt::VlltWrite>();
            auto tp = v.get_ref_tuple(vllt::table_index_t{row});
            bw ^= std::get<0>(tp);                   // keep load live
            __asm__ volatile("" : "+r"(bw));         // prevent dead-store elision
        }
        uint64_t t1 = rdtsc();
        lat[i] = t1 - t0;
    }

    uint64_t wall1 = rdtsc();
    done.arrive_and_wait();
    return { checksum, std::move(lat), wall1 - wall0 };
}

// ---------------------------------------------------------------------------
// Worker: mutex mode (VLLT_SYNC_EXTERNAL + std::mutex)
// ---------------------------------------------------------------------------

BenchResult run_mutex(
    vllt::VlltStaticTable<DATA, vllt::sync_t::VLLT_SYNC_EXTERNAL, BLOCK_SIZE>& table,
    std::mutex& mtx,
    int      tid,
    uint64_t ops,
    uint32_t work_per_op,
    uint32_t write_pct,
    uint64_t warmup,
    std::latch& go,
    std::latch& done)
{
    for (uint64_t i = 0; i < warmup; ++i) {
        uint64_t s = uint64_t(tid + 1000) * warmup + i + 1;
        uint64_t bw = busy_work(s, work_per_op);
        uint32_t row = uint32_t(s % TABLE_ROWS);
        if ((i % 100) < write_pct) {
            std::lock_guard lk{mtx};
            auto v = table.view<vllt::VlltWrite, uint64_t>();
            auto tp = v.get_ref_tuple(vllt::table_index_t{row});
            std::get<0>(tp) = bw & 0xFFFF;
        } else {
            std::lock_guard lk{mtx};
            auto v = table.view<uint64_t, vllt::VlltWrite>();
            auto tp = v.get_ref_tuple(vllt::table_index_t{row});
            bw ^= std::get<0>(tp);
        }
        (void)bw;
    }

    go.arrive_and_wait();
    uint64_t wall0 = rdtsc();

    uint64_t checksum = 0;
    std::vector<uint64_t> lat(ops);

    for (uint64_t i = 0; i < ops; ++i) {
        uint64_t s   = uint64_t(tid) * ops + i + 1;  // +1: seed=0 → bw=0 (xorshift degeneracy)
        uint64_t bw  = busy_work(s, work_per_op);
        checksum    ^= bw;                           // deterministic, before table op
        uint32_t row = uint32_t(s % TABLE_ROWS);

        uint64_t t0 = rdtsc();
        if ((i % 100) < write_pct) {
            std::lock_guard lk{mtx};
            auto v  = table.view<vllt::VlltWrite, uint64_t>();
            auto tp = v.get_ref_tuple(vllt::table_index_t{row});
            std::get<0>(tp) = bw & 0xFFFF;
        } else {
            std::lock_guard lk{mtx};
            auto v  = table.view<uint64_t, vllt::VlltWrite>();
            auto tp = v.get_ref_tuple(vllt::table_index_t{row});
            bw ^= std::get<0>(tp);
            __asm__ volatile("" : "+r"(bw));
        }
        uint64_t t1 = rdtsc();
        lat[i] = t1 - t0;
    }

    uint64_t wall1 = rdtsc();
    done.arrive_and_wait();
    return { checksum, std::move(lat), wall1 - wall0 };
}

// ---------------------------------------------------------------------------
// Aggregate results from one repetition
// ---------------------------------------------------------------------------

struct RunStats {
    double   tput;     // ops/sec
    double   p50_ns;
    double   p99_ns;
    uint64_t checksum;
};

static RunStats aggregate(std::vector<BenchResult>& res, uint64_t total_ops) {
    uint64_t max_wall = 0;
    for (auto& r : res) max_wall = std::max(max_wall, r.wall);
    double wall_ns = (double)max_wall * g_ns_per_cycle;

    std::vector<uint64_t> all;
    all.reserve(total_ops);
    for (auto& r : res) all.insert(all.end(), r.lat.begin(), r.lat.end());
    std::sort(all.begin(), all.end());

    uint64_t cs = 0;
    for (auto& r : res) cs ^= r.checksum;

    return {
        (double)total_ops / (wall_ns * 1e-9),
        all[all.size() / 2]    * g_ns_per_cycle,
        all[(size_t)(all.size() * 0.99)] * g_ns_per_cycle,
        cs
    };
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: bench <lockless|mutex> <threads> <work_per_op> <read_pct>"
                     " [total_ops] [repeats]\n";
        return 1;
    }
    std::string mode  = argv[1];
    int         T     = std::atoi(argv[2]);
    uint32_t    work  = (uint32_t)std::atoi(argv[3]);
    uint32_t    rpct  = (uint32_t)std::atoi(argv[4]);
    uint64_t    total = (argc >= 6) ? (uint64_t)std::atoll(argv[5]) : 1'000'000ULL;
    int         reps  = (argc >= 7) ? std::atoi(argv[6]) : 5;

    if (mode != "lockless" && mode != "mutex") { std::cerr << "bad mode\n"; return 1; }
    if (T < 1 || rpct > 100)                  { std::cerr << "bad args\n"; return 1; }

    uint32_t wpct    = 100 - rpct;
    uint64_t per_thr = total / (uint64_t)T;
    total            = per_thr * (uint64_t)T;
    uint64_t warmup  = std::max(per_thr / 10, uint64_t(500));

    calibrate_tsc();

    std::vector<RunStats> stats;
    stats.reserve(reps);

    for (int rep = 0; rep < reps; ++rep) {
        std::latch go{T}, done{T};
        std::vector<BenchResult> res(T);

        if (mode == "lockless") {
            vllt::VlltStaticTable<DATA, vllt::sync_t::VLLT_SYNC_INTERNAL, BLOCK_SIZE> table;
            populate(table);
            {
                std::vector<std::jthread> ts;
                for (int t = 0; t < T; ++t)
                    ts.emplace_back([&,t](){
                        res[t] = run_lockless(table, t, per_thr, work, wpct, warmup, go, done);
                    });
            }
        } else {
            vllt::VlltStaticTable<DATA, vllt::sync_t::VLLT_SYNC_EXTERNAL, BLOCK_SIZE> table;
            std::mutex mtx;
            populate(table);
            {
                std::vector<std::jthread> ts;
                for (int t = 0; t < T; ++t)
                    ts.emplace_back([&,t](){
                        res[t] = run_mutex(table, mtx, t, per_thr, work, wpct, warmup, go, done);
                    });
            }
        }
        stats.push_back(aggregate(res, total));
    }

    // pick median-throughput rep
    std::sort(stats.begin(), stats.end(),
              [](const RunStats& a, const RunStats& b){ return a.tput < b.tput; });
    auto& s = stats[stats.size() / 2];

    std::cout << std::fixed << std::setprecision(0)
              << mode << ","
              << T << "," << work << "," << rpct << "," << total << ","
              << (uint64_t)s.tput << ","
              << (uint64_t)s.p50_ns << ","
              << (uint64_t)s.p99_ns << ","
              << std::hex << s.checksum << "\n";
    return 0;
}
