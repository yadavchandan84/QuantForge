#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "quantforge/engine.hpp"
#include "quantforge/execution_handler.hpp"
#include "quantforge/strategy.hpp"
#include "quantforge/thread_pool.hpp"

namespace qf {

/// The per-job components a sweep worker needs to run one backtest. Each job
/// owns its own data handler, strategy, and execution handler so there is no
/// shared mutable state between threads.
struct SweepJob {
    std::unique_ptr<DataHandler> data;
    std::unique_ptr<Strategy> strategy;
    ExecutionHandler execution;
    BacktestConfig config;
};

/// Factory that builds job `index` of a sweep. The factory must be
/// deterministic: given the same index it must produce identical components
/// (including the config seed), which is what makes the sweep's results
/// independent of thread count and scheduling.
using SweepJobFactory = std::function<SweepJob(std::size_t index)>;

/// Runs `num_jobs` backtests, building each via `factory`, across `num_threads`
/// worker threads. Returns results indexed by job number (result[i] always
/// corresponds to factory(i)), so the output is identical for any thread count.
///
/// Determinism guarantees:
///   * each job is independent (own data/strategy/execution, no sharing);
///   * each job's RNG seed comes from its config (set by the factory), not from
///     wall-clock or thread id;
///   * results are placed at a fixed index, never appended in completion order.
inline std::vector<BacktestResult> runSweep(std::size_t num_jobs,
                                            const SweepJobFactory& factory,
                                            std::size_t num_threads) {
    std::vector<BacktestResult> results(num_jobs);

    if (num_jobs == 0) {
        return results;
    }
    if (num_threads <= 1) {
        for (std::size_t i = 0; i < num_jobs; ++i) {
            SweepJob job = factory(i);
            Engine engine(*job.data, *job.strategy, std::move(job.execution),
                          job.config);
            results[i] = engine.run();
        }
        return results;
    }

    ThreadPool pool(num_threads);
    std::atomic<std::size_t> remaining{num_jobs};
    std::mutex done_mutex;
    std::condition_variable done_cv;

    for (std::size_t i = 0; i < num_jobs; ++i) {
        pool.submit([i, &factory, &results, &remaining, &done_mutex, &done_cv] {
            SweepJob job = factory(i);
            Engine engine(*job.data, *job.strategy, std::move(job.execution),
                          job.config);
            results[i] = engine.run();  // fixed index -> order-independent
            if (remaining.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lock(done_mutex);
                done_cv.notify_one();
            }
        });
    }

    std::unique_lock<std::mutex> lock(done_mutex);
    done_cv.wait(lock, [&remaining] { return remaining.load() == 0; });
    return results;
}

}  // namespace qf
