#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace qf {

/// A minimal fixed-size thread pool. Tasks are std::function<void()> run on a
/// pool of worker threads. Used by parameter sweeps; kept dependency-free so it
/// also works under sanitizers in CI.
class ThreadPool {
  public:
    explicit ThreadPool(std::size_t num_threads) {
        if (num_threads == 0) {
            num_threads = 1;
        }
        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Enqueues a task for execution.
    void submit(std::function<void()> task) {
        {
            std::unique_lock lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    std::size_t size() const noexcept { return workers_.size(); }

  private:
    void workerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_{false};
};

}  // namespace qf
