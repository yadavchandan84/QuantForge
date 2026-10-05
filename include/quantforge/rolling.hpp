#pragma once

#include <cmath>
#include <cstddef>
#include <deque>
#include <stdexcept>

#include "quantforge/types.hpp"

namespace qf {

/// Fixed-size rolling window maintaining sum and sum-of-squares incrementally,
/// so mean() and stdev() are O(1) per update. Used by moving-average and
/// mean-reversion strategies on the hot path.
class RollingWindow {
  public:
    explicit RollingWindow(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("RollingWindow capacity must be > 0");
        }
    }

    /// Adds a value, evicting the oldest if the window is full.
    void push(double x) {
        buf_.push_back(x);
        sum_ += x;
        sumsq_ += x * x;
        if (buf_.size() > capacity_) {
            const double old = buf_.front();
            buf_.pop_front();
            sum_ -= old;
            sumsq_ -= old * old;
        }
    }

    bool full() const noexcept { return buf_.size() == capacity_; }
    std::size_t size() const noexcept { return buf_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }

    double mean() const {
        if (buf_.empty()) {
            return 0.0;
        }
        return sum_ / static_cast<double>(buf_.size());
    }

    /// Sample standard deviation (N-1). Returns 0 for fewer than 2 points.
    double stdev() const {
        const auto n = static_cast<double>(buf_.size());
        if (n < 2.0) {
            return 0.0;
        }
        const double m = sum_ / n;
        // Guard against tiny negative values from floating point cancellation.
        const double var = (sumsq_ - n * m * m) / (n - 1.0);
        return var > 0.0 ? std::sqrt(var) : 0.0;
    }

    void clear() {
        buf_.clear();
        sum_ = 0.0;
        sumsq_ = 0.0;
    }

  private:
    std::size_t capacity_;
    std::deque<double> buf_;
    double sum_{0.0};
    double sumsq_{0.0};
};

}  // namespace qf
