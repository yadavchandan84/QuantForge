#include "quantforge/strategies.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "quantforge/event.hpp"
#include "quantforge/rolling.hpp"
#include "quantforge/strategy.hpp"
#include "quantforge/types.hpp"

using namespace qf;

namespace {

MarketEvent bar(Timestamp ts, Price close, SymbolId sym = 0) {
    MarketEvent m;
    m.ts = ts;
    m.symbol = sym;
    m.kind = MarketKind::Bar;
    m.close = close;
    return m;
}

// Drives a strategy over a price path and collects emitted signals.
std::vector<SignalEvent> run(Strategy& strat, const std::vector<Price>& prices,
                             SymbolId sym = 0) {
    std::vector<SignalEvent> signals;
    auto sink = [&signals](const SignalEvent& s) { signals.push_back(s); };
    Timestamp ts = 0;
    for (Price p : prices) {
        StrategyContext ctx(ts, sink, strat.id());
        strat.onMarket(bar(ts, p, sym), ctx);
        ts += kNanosPerDay;
    }
    return signals;
}

}  // namespace

// ---- RollingWindow -------------------------------------------------------
TEST(RollingWindow, MeanAndStdev) {
    RollingWindow w(3);
    w.push(2.0);
    w.push(4.0);
    w.push(6.0);
    EXPECT_TRUE(w.full());
    EXPECT_DOUBLE_EQ(w.mean(), 4.0);
    // sample stdev of {2,4,6} = 2.0
    EXPECT_DOUBLE_EQ(w.stdev(), 2.0);

    // Rolls: drop 2, add 8 -> {4,6,8}, mean 6
    w.push(8.0);
    EXPECT_DOUBLE_EQ(w.mean(), 6.0);
    EXPECT_DOUBLE_EQ(w.stdev(), 2.0);
}

TEST(RollingWindow, StdevZeroForConstant) {
    RollingWindow w(4);
    for (int i = 0; i < 4; ++i) w.push(5.0);
    EXPECT_DOUBLE_EQ(w.stdev(), 0.0);
}

// ---- MovingAverageCrossover ---------------------------------------------
TEST(MovingAverageCrossover, EmitsLongThenShortOnCrosses) {
    MovingAverageCrossover strat(0, /*fast=*/2, /*slow=*/4);
    // Rising then falling path to force an up-cross then a down-cross.
    std::vector<Price> prices{10, 10, 10, 10, 12, 14, 16, 18,
                              16, 12, 8,  6,  4,  2};
    auto sig = run(strat, prices);

    ASSERT_GE(sig.size(), 2u);
    // First actionable signal is Long (fast rises above slow), later a Short.
    EXPECT_EQ(sig.front().direction, SignalDirection::Long);
    bool saw_short = false;
    for (const auto& s : sig) {
        if (s.direction == SignalDirection::Short) saw_short = true;
    }
    EXPECT_TRUE(saw_short);
}

TEST(MovingAverageCrossover, IgnoresOtherSymbols) {
    MovingAverageCrossover strat(/*symbol=*/7, 2, 4);
    auto sig = run(strat, {1, 2, 3, 4, 5, 6, 7, 8}, /*sym=*/3);
    EXPECT_TRUE(sig.empty());
}

TEST(MovingAverageCrossover, RejectsBadParams) {
    EXPECT_THROW({ MovingAverageCrossover(0, 4, 4); }, std::invalid_argument);
    EXPECT_THROW({ MovingAverageCrossover(0, 5, 2); }, std::invalid_argument);
    EXPECT_THROW({ MovingAverageCrossover(0, 0, 2); }, std::invalid_argument);
}

// ---- MeanReversion -------------------------------------------------------
TEST(MeanReversion, ShortsSpikeAboveMeanThenExits) {
    MeanReversion strat(0, /*lookback=*/5, /*entry_z=*/1.5, /*exit_z=*/0.5);
    // Flat around 100, then a spike up (should short), then revert (exit).
    std::vector<Price> prices{100, 100, 100, 100, 100, 110, 100, 100};
    auto sig = run(strat, prices);

    ASSERT_FALSE(sig.empty());
    // The spike at index 5 is well above the mean of the prior window -> Short.
    EXPECT_EQ(sig.front().direction, SignalDirection::Short);
    // Eventually it should flatten back out.
    EXPECT_EQ(sig.back().direction, SignalDirection::Exit);
}

TEST(MeanReversion, LongsDipBelowMean) {
    MeanReversion strat(0, 5, 1.5, 0.5);
    std::vector<Price> prices{100, 100, 100, 100, 100, 90, 100};
    auto sig = run(strat, prices);
    ASSERT_FALSE(sig.empty());
    EXPECT_EQ(sig.front().direction, SignalDirection::Long);
}

TEST(MeanReversion, RejectsBadParams) {
    EXPECT_THROW({ MeanReversion(0, 5, 1.0, 1.0); }, std::invalid_argument);
    EXPECT_THROW({ MeanReversion(0, 5, 0.0, 0.0); }, std::invalid_argument);
}

// ---- MarketMaker ---------------------------------------------------------
TEST(MarketMaker, LeansLongBelowFairAndShortAboveFair) {
    MarketMaker strat(0, /*fair_lookback=*/4, /*band=*/0.02,
                      /*max_inventory=*/5.0);
    // Establish fair ~100, then dip (lean long), then rip (lean short).
    std::vector<Price> prices{100, 100, 100, 100, 95, 100, 106};
    auto sig = run(strat, prices);

    ASSERT_FALSE(sig.empty());
    bool saw_long = false, saw_short = false;
    for (const auto& s : sig) {
        if (s.direction == SignalDirection::Long) saw_long = true;
        if (s.direction == SignalDirection::Short) saw_short = true;
    }
    EXPECT_TRUE(saw_long);
    EXPECT_TRUE(saw_short);
}

TEST(MarketMaker, RejectsBadParams) {
    EXPECT_THROW({ MarketMaker(0, 4, 0.0, 5.0); }, std::invalid_argument);
    EXPECT_THROW({ MarketMaker(0, 4, 0.02, 0.0); }, std::invalid_argument);
}

// ---- Reset reproducibility ----------------------------------------------
TEST(Strategy, ResetReproducesSignals) {
    MovingAverageCrossover strat(0, 2, 4);
    std::vector<Price> prices{10, 10, 10, 10, 12, 14, 16, 8, 4, 2};

    auto first = run(strat, prices);
    strat.reset();
    auto second = run(strat, prices);

    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].direction, second[i].direction);
        EXPECT_EQ(first[i].ts, second[i].ts);
    }
}
