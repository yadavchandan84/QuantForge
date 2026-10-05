#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "quantforge/data_handler.hpp"
#include "quantforge/engine.hpp"
#include "quantforge/execution_handler.hpp"
#include "quantforge/execution_models.hpp"
#include "quantforge/strategies.hpp"
#include "quantforge/sweep.hpp"
#include "quantforge/types.hpp"

using namespace qf;

namespace {

// Builds a deterministic sinusoid-ish price path so trend strategies trade.
std::vector<MarketEvent> makeBars(std::size_t n, SymbolId sym = 0) {
    std::vector<MarketEvent> bars;
    bars.reserve(n);
    Timestamp ts = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double base =
            100.0 + 10.0 * std::sin(static_cast<double>(i) * 0.3);
        MarketEvent m;
        m.ts = ts;
        m.symbol = sym;
        m.kind = MarketKind::Bar;
        m.open = base;
        m.high = base + 1.0;
        m.low = base - 1.0;
        m.close = base;
        m.volume = 1'000'000.0;
        bars.push_back(m);
        ts += kNanosPerDay;
    }
    return bars;
}

std::unique_ptr<ExecutionHandler> makeExec(std::uint64_t seed) {
    return std::make_unique<ExecutionHandler>(
        std::make_unique<FixedLatency>(0),
        std::make_unique<FixedBpsSlippage>(1.0),
        std::make_unique<PerShareFee>(0.005), ExecutionConfig{}, seed);
}

}  // namespace

TEST(Engine, RunsEndToEndAndProducesResults) {
    auto bars = makeBars(60);
    CsvBarDataHandler data(bars);
    MovingAverageCrossover strat(0, 3, 10);
    auto exec = makeExec(42);

    BacktestConfig cfg;
    cfg.initial_cash = 100'000.0;
    cfg.target_position = 100.0;

    Engine engine(data, strat, std::move(*exec), cfg);
    auto res = engine.run();

    EXPECT_EQ(res.num_market_events, 60u);
    EXPECT_GT(res.num_signals, 0u);
    EXPECT_GT(res.num_fills, 0u);
    EXPECT_FALSE(res.equity_curve.empty());
    // Equity curve has one point per distinct timestamp (all distinct here).
    EXPECT_EQ(res.equity_curve.size(), 60u);
    // Starting equity equals initial cash (first bar, before any trade cost).
    EXPECT_NEAR(res.equity_curve.front().equity, 100'000.0, 1e-6);
    // Report fields are populated.
    EXPECT_EQ(res.report.num_periods, 59u);
}

TEST(Engine, RepeatedRunsAreIdentical) {
    auto bars = makeBars(80);
    CsvBarDataHandler data(bars);
    MeanReversion strat(0, 10, 1.5, 0.5);
    auto exec = makeExec(7);

    BacktestConfig cfg;
    Engine engine(data, strat, std::move(*exec), cfg);

    auto a = engine.run();
    auto b = engine.run();  // same engine, re-run

    ASSERT_EQ(a.equity_curve.size(), b.equity_curve.size());
    for (std::size_t i = 0; i < a.equity_curve.size(); ++i) {
        EXPECT_EQ(a.equity_curve[i].equity, b.equity_curve[i].equity);
    }
    EXPECT_EQ(a.final_equity, b.final_equity);
    EXPECT_EQ(a.num_fills, b.num_fills);
}

// The central Phase-7 guarantee: a parameter sweep yields identical results
// no matter how many threads run it.
TEST(Sweep, DeterministicAcrossThreadCounts) {
    // Each job: a different fast/slow MA pair, with a seed tied to the index.
    const std::vector<std::pair<std::size_t, std::size_t>> params = {
        {2, 10}, {3, 15}, {5, 20}, {4, 12}, {6, 25}, {2, 8}, {7, 30}, {3, 9}};
    const std::size_t num_jobs = params.size();

    SweepJobFactory factory = [&params](std::size_t i) {
        BacktestConfig cfg;
        cfg.seed = 1000 + static_cast<std::uint64_t>(i);
        cfg.target_position = 100.0;
        return SweepJob{
            std::make_unique<CsvBarDataHandler>(makeBars(120)),
            std::make_unique<MovingAverageCrossover>(0, params[i].first,
                                                     params[i].second),
            std::move(*makeExec(/*seed=*/1000 + static_cast<std::uint64_t>(i))),
            cfg};
    };

    auto r1 = runSweep(num_jobs, factory, /*threads=*/1);
    auto r2 = runSweep(num_jobs, factory, /*threads=*/2);
    auto r4 = runSweep(num_jobs, factory, /*threads=*/4);
    auto r8 = runSweep(num_jobs, factory, /*threads=*/8);

    ASSERT_EQ(r1.size(), num_jobs);
    for (std::size_t i = 0; i < num_jobs; ++i) {
        // Final equity must be bit-identical across all thread counts.
        EXPECT_EQ(r1[i].final_equity, r2[i].final_equity) << "job " << i;
        EXPECT_EQ(r1[i].final_equity, r4[i].final_equity) << "job " << i;
        EXPECT_EQ(r1[i].final_equity, r8[i].final_equity) << "job " << i;

        EXPECT_EQ(r1[i].num_fills, r8[i].num_fills) << "job " << i;
        EXPECT_EQ(r1[i].equity_curve.size(), r8[i].equity_curve.size());

        // Full equity path identical (strongest check).
        for (std::size_t k = 0; k < r1[i].equity_curve.size(); ++k) {
            EXPECT_EQ(r1[i].equity_curve[k].equity, r8[i].equity_curve[k].equity)
                << "job " << i << " point " << k;
        }
    }
}

TEST(Sweep, SingleThreadMatchesPerJobEngine) {
    SweepJobFactory factory = [](std::size_t i) {
        BacktestConfig cfg;
        cfg.seed = static_cast<std::uint64_t>(i);
        return SweepJob{std::make_unique<CsvBarDataHandler>(makeBars(50)),
                        std::make_unique<MovingAverageCrossover>(0, 2, 10),
                        std::move(*makeExec(static_cast<std::uint64_t>(i))),
                        cfg};
    };
    auto swept = runSweep(3, factory, 4);
    ASSERT_EQ(swept.size(), 3u);
    for (auto& r : swept) {
        EXPECT_EQ(r.num_market_events, 50u);
    }
}
