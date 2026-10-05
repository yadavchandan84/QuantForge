// Micro-benchmark: raw push/pop throughput of the event queue.
// Reports only measured numbers; no synthetic or hard-coded results.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "quantforge/event_queue.hpp"

using namespace qf;

int main(int argc, char** argv) {
    std::size_t n = 1'000'000;
    if (argc > 1) {
        n = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    }

    // Pre-generate pseudo-random timestamps so the queue does real ordering
    // work rather than inserting an already-sorted stream.
    std::mt19937_64 rng(12345);
    std::vector<Timestamp> stamps(n);
    for (auto& t : stamps) {
        t = static_cast<Timestamp>(rng() % (n * 10));
    }

    EventQueue q;
    const auto t0 = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < n; ++i) {
        MarketEvent m;
        m.ts = stamps[i];
        m.kind = MarketKind::Bar;
        m.close = static_cast<Price>(i);
        q.emit(m);
    }

    Timestamp last = std::numeric_limits<Timestamp>::min();
    bool ordered = true;
    std::size_t popped = 0;
    while (auto ev = q.pop()) {
        if (ev->ts < last) {
            ordered = false;
        }
        last = ev->ts;
        ++popped;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    const double eps = static_cast<double>(2 * n) / secs;  // push + pop ops

    std::printf("event_queue benchmark\n");
    std::printf("  events           : %zu\n", n);
    std::printf("  ordered output   : %s\n", ordered ? "yes" : "NO");
    std::printf("  popped           : %zu\n", popped);
    std::printf("  elapsed          : %.4f s\n", secs);
    std::printf("  push+pop ops/sec : %.0f\n", eps);
    std::printf("  events/sec       : %.0f\n", static_cast<double>(n) / secs);

    return ordered ? 0 : 1;
}
