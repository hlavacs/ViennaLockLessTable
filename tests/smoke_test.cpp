#include <iostream>
#include <thread>
#include <latch>
#include <mutex>
#include <vector>
#include <cassert>
#include "VLLT.h"

using DATA = vtll::tl<int, double>;

// --- Mode A: CAS-only lockless via VLLT_SYNC_INTERNAL_PUSHBACK ---
// Multiple threads hold simultaneous pushback-only views; the table's
// atomic size counter is the only synchronisation primitive.
void lockless_test() {
    vllt::VlltStaticTable<DATA, vllt::sync_t::VLLT_SYNC_INTERNAL_PUSHBACK, 1<<5> table;

    constexpr int N_THREADS = 4, N_PER = 25;
    std::latch go{N_THREADS};

    auto writer = [&](int id) {
        go.arrive_and_wait();
        auto view = table.template view<vllt::VlltWrite>(); // pushback-only view
        for (int i = 0; i < N_PER; ++i)
            view.push_back(id, (double)i);
    };

    { std::vector<std::jthread> ts; for (int i=0;i<N_THREADS;++i) ts.emplace_back(writer, i); }

    auto view = table.view<>();
    uint64_t expected = N_THREADS * N_PER;
    assert(view.size().value() == expected);

    auto row0 = view.get_ref_tuple(vllt::table_index_t{0});
    std::cout << "[lockless] size=" << view.size().value()
              << "  row[0]: id=" << vllt::get<int&>(row0)
              << " val=" << vllt::get<double&>(row0) << '\n';
}

// --- Mode B: VLLT_SYNC_EXTERNAL + std::mutex ---
// VLLT does zero internal sync; a plain mutex serialises every view
// creation and use, making it the reference "mutex mode".
void mutex_test() {
    vllt::VlltStaticTable<DATA, vllt::sync_t::VLLT_SYNC_EXTERNAL, 1<<5> table;
    std::mutex mtx;

    constexpr int N_THREADS = 4, N_PER = 25;
    std::latch go{N_THREADS};

    auto worker = [&](int id) {
        go.arrive_and_wait();
        for (int i = 0; i < N_PER; ++i) {
            std::lock_guard lock{mtx};
            auto view = table.view<>();
            view.push_back(id, (double)i);
        }
    };

    { std::vector<std::jthread> ts; for (int i=0;i<N_THREADS;++i) ts.emplace_back(worker, i); }

    auto view = table.view<>();
    uint64_t expected = N_THREADS * N_PER;
    assert(view.size().value() == expected);

    auto row0 = view.get_ref_tuple(vllt::table_index_t{0});
    std::cout << "[mutex]    size=" << view.size().value()
              << "  row[0]: id=" << vllt::get<int&>(row0)
              << " val=" << vllt::get<double&>(row0) << '\n';
}

int main() {
    lockless_test();
    mutex_test();
    std::cout << "Both modes OK.\n";
}
