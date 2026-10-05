// Full-pipeline throughput benchmark: data -> strategy -> execution ->
// portfolio -> analytics, over N synthetic bars. Reports only measured numbers.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "quantforge/data_handler.hpp"
#include "quantforge/engine.hpp"
#include "quantforge/execution_handler.hpp"
#include "quantforge/execution_models.hpp"
#include "quantforge/strategies.hpp"
#include "quantforge/types.hpp"

using namespace qf;

static std::vector<MarketEvent> makeBars(std::size_t n) {
    std::vector<MarketEvent> bars;
    bars.reserve(n);
    Timestamp ts = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double base = 100.0 + 10.0 * std::sin(static_cast<double>(i) * 0.01) +
                            5.0 * std::sin(static_cast<double>(i) * 0.013);
        MarketEvent m;
        m.ts = ts;
        m.symbol = 0;
        m.kind = MarketKind::Bar;
        m.open = base;
        m.high = base + 0.5;
        m.low = base - 0.5;
        m.close = base;
        m.volume = 1'000'000.0;
        bars.push_back(m);
        ts += kNanosPerMinute;
    }
    return bars;
}

int main(int argc, char** argv) {
    std::size_t n = 2'000'000;
    if (argc > 1) {
        n = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    }

    auto bars = makeBars(n);
    CsvBarDataHandler data(std::move(bars));
    MovingAverageCrossover strat(0, 20, 60);

    ExecutionConfig ecfg;
    ExecutionHandler exec(std::make_unique<FixedLatency>(0),
                          std::make_unique<FixedBpsSlippage>(1.0),
                          std::make_unique<PerShareFee>(0.005), ecfg, 42);

    BacktestConfig cfg;
    cfg.record_equity_curve = true;

    Engine engine(data, strat, std::move(exec), cfg);

    const auto t0 = std::chrono::steady_clock::now();
    auto res = engine.run();
    const auto t1 = std::chrono::steady_clock::now();

    const double secs = std::chrono::duration<double>(t1 - t0).count();
    const std::size_t total_events =
        res.num_market_events + res.num_signals + res.num_orders + res.num_fills;

    std::printf("engine benchmark (full pipeline)\n");
    std::printf("  bars              : %zu\n", n);
    std::printf("  market events     : %zu\n", res.num_market_events);
    std::printf("  signals           : %zu\n", res.num_signals);
    std::printf("  orders            : %zu\n", res.num_orders);
    std::printf("  fills             : %zu\n", res.num_fills);
    std::printf("  total events      : %zu\n", total_events);
    std::printf("  elapsed           : %.4f s\n", secs);
    if (secs > 0.0) {
        std::printf("  bars/sec          : %.0f\n", static_cast<double>(n) / secs);
        std::printf("  events/sec        : %.0f\n", static_cast<double>(total_events) / secs);
    }
    std::printf("  final equity      : %.2f\n", res.final_equity);
    std::printf("  sharpe            : %.4f\n", res.report.sharpe);

    return 0;
}
