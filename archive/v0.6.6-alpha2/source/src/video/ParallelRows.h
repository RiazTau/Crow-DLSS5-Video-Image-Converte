#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <utility>
#include <vector>

namespace video::perf {

inline unsigned ConfiguredThreadLimit() noexcept {
    if (const char* value = std::getenv("DLSS5_PERF_THREADS")) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end != value && parsed >= 1ul && parsed <= 64ul) {
            return static_cast<unsigned>(parsed);
        }
    }
    const unsigned hw = std::thread::hardware_concurrency();
    // The temporal stages are memory-heavy. Going beyond 16 workers tends to add
    // scheduling/cache pressure faster than it adds throughput on typical desktop/laptop CPUs.
    return std::clamp(hw ? hw : 4u, 1u, 16u);
}

inline unsigned WorkerCountForRows(uint32_t rows, uint32_t minRowsPerWorker = 48u) noexcept {
    if (rows == 0) return 1u;
    const unsigned limit = ConfiguredThreadLimit();
    const unsigned useful = std::max(1u, static_cast<unsigned>((rows + minRowsPerWorker - 1u) / minRowsPerWorker));
    return std::max(1u, std::min(limit, useful));
}

template <typename Fn>
void ParallelForRows(uint32_t rows, uint32_t minRowsPerWorker, Fn&& fn) {
    const unsigned workers = WorkerCountForRows(rows, minRowsPerWorker);
    if (workers <= 1u) {
        fn(0u, rows, 0u);
        return;
    }

    std::vector<std::jthread> threads;
    threads.reserve(workers);
    uint32_t begin = 0;
    for (unsigned worker = 0; worker < workers; ++worker) {
        const uint32_t remainingRows = rows - begin;
        const unsigned remainingWorkers = workers - worker;
        const uint32_t count = (remainingRows + remainingWorkers - 1u) / remainingWorkers;
        const uint32_t end = std::min(rows, begin + count);
        threads.emplace_back([begin, end, worker, &fn]() {
            fn(begin, end, worker);
        });
        begin = end;
    }
    // std::jthread joins on destruction.
}

template <typename Fn>
void ParallelForRows(uint32_t rows, Fn&& fn) {
    ParallelForRows(rows, 48u, std::forward<Fn>(fn));
}

} // namespace video::perf
