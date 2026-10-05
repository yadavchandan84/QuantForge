// Phase 8: deterministic replay.
//
// These tests specifically exercise the *randomized* execution path
// (RandomLatency), which is where a sloppy implementation would leak
// non-determinism (unseeded RNG, wall-clock seeding, or thread-dependent
// ordering). A fixed seed must give identical results on every replay and at
// any thread count, while different seeds must actually diverge (proving the
// RNG is really being used).
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

std::vector<MarketEvent> makeBars(std::size_t n, SymbolId sym = 0) {
    std::vector<MarketEvent> bars;
    bars.reserve(n);
    Timestamp ts = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double base =
            100.0 + 8.0 * std::sin(static_cast<double>(i) * 0.25) +
            3.0 * std::cos(static_cast<double>(i) * 0.11);
        MarketEvent m;
        m.ts = ts;
        m.symbol = sym;
        m.kind = MarketKind::Bar;
        m.open = base;
        m.high = base + 1.0;
        m.low = base - 1.0;
        m.close = base;
        m.volume = 500'000.0;
        bars.push_back(m);
        ts += kNanosPerDay;
    }
    return bars;
}

// Execution handler WITH randomness: random latency means the RNG stream must
// be reproduced exactly for results to match.
ExecutionHandler randomExec(std::uint64_t seed) {
    // Random slippage (price) + random latency (timing) both consume the RNG,
    // so the seed materially changes fills -- making replay determinism a
    // meaningful guarantee and different seeds genuinely diverge.
    return ExecutionHandler(
        std::make_unique<RandomLatency>(0, 5 * kNanosPerMinute),
        std::make_unique<RandomBpsSlippage>(2.0, 25.0),
        std::make_unique<PerShareFee>(0.005), ExecutionConfig{}, seed);
}

bool sameResult(const BacktestResult& a, const BacktestResult& b) {
    if (a.equity_curve.size() != b.equity_curve.size()) return false;
    if (a.final_equity != b.final_equity) return false;
    if (a.num_fills != b.num_fills) return false;
    for (std::size_t i = 0; i < a.equity_curve.size(); ++i) {
        if (a.equity_curve[i].ts != b.equity_curve[i].ts) return false;
        if (a.equity_curve[i].equity != b.equity_curve[i].equity) return false;
    }
    return true;
}

}  // namespace

TEST(Determinism, RandomLatencyReplayIsBitIdentical) {
    auto bars = makeBars(100);
    CsvBarDataHandler data(bars);
    MovingAverageCrossover strat(0, 4, 16);

    BacktestConfig cfg;
    cfg.seed = 20240607;  // fixed seed

    Engine engine(data, strat, randomExec(cfg.seed), cfg);

    auto r1 = engine.run();
    auto r2 = engine.run();
    auto r3 = engine.run();

    EXPECT_TRUE(sameResult(r1, r2));
    EXPECT_TRUE(sameResult(r1, r3));
    // Sanity: randomness is in play, so there must actually be fills.
    EXPECT_GT(r1.num_fills, 0u);
}

TEST(Determinism, DifferentSeedsDiverge) {
    auto bars = makeBars(100);
    CsvBarDataHandler data(bars);
    MovingAverageCrossover strat(0, 4, 16);

    BacktestConfig cfg_a;
    cfg_a.seed = 1;
    Engine ea(data, strat, randomExec(cfg_a.seed), cfg_a);
    auto ra = ea.run();

    CsvBarDataHandler data2(bars);
    MovingAverageCrossover strat2(0, 4, 16);
    BacktestConfig cfg_b;
    cfg_b.seed = 999999;
    Engine eb(data2, strat2, randomExec(cfg_b.seed), cfg_b);
    auto rb = eb.run();

    // Latency differences shift fill timestamps/prices, so the equity paths
    // should not be bit-identical. (Count of fills may coincide; the paths
    // must differ somewhere.)
    EXPECT_FALSE(sameResult(ra, rb));
}

TEST(Determinism, SweepWithRandomnessIdenticalAcrossThreads) {
    const std::size_t num_jobs = 12;

    SweepJobFactory factory = [](std::size_t i) {
        BacktestConfig cfg;
        cfg.seed = 500 + static_cast<std::uint64_t>(i);  // seed tied to index
        cfg.target_position = 100.0;
        return SweepJob{
            std::make_unique<CsvBarDataHandler>(makeBars(150)),
            std::make_unique<MovingAverageCrossover>(0, 3 + i % 4, 12 + i % 6),
            randomExec(cfg.seed), cfg};
    };

    auto r1 = runSweep(num_jobs, factory, 1);
    auto r3 = runSweep(num_jobs, factory, 3);
    auto r8 = runSweep(num_jobs, factory, 8);

    ASSERT_EQ(r1.size(), num_jobs);
    for (std::size_t i = 0; i < num_jobs; ++i) {
        EXPECT_TRUE(sameResult(r1[i], r3[i])) << "job " << i << " (1 vs 3)";
        EXPECT_TRUE(sameResult(r1[i], r8[i])) << "job " << i << " (1 vs 8)";
    }
}

TEST(Determinism, ReseedRestoresExactStream) {
    // The execution handler alone must reproduce its latency draws after a
    // reseed, independent of the engine.
    auto h = randomExec(314159);
    OrderEvent o;
    o.ts = 0;
    o.symbol = 0;
    o.side = Side::Buy;
    o.type = OrderType::Market;
    o.quantity = 10.0;
    MarketSnapshot snap;
    snap.reference = 100.0;
    snap.volume = 1'000'000.0;

    std::vector<Timestamp> first;
    for (int i = 0; i < 20; ++i) {
        auto f = h.execute(o, snap);
        ASSERT_TRUE(f.has_value());
        first.push_back(f->ts);
    }

    h.reseed(314159);
    std::vector<Timestamp> second;
    for (int i = 0; i < 20; ++i) {
        auto f = h.execute(o, snap);
        ASSERT_TRUE(f.has_value());
        second.push_back(f->ts);
    }

    EXPECT_EQ(first, second);
}
