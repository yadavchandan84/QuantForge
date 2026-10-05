// Phase 11: end-to-end known-answer PnL + engine-level no-lookahead.
//
// These tests run the *whole* pipeline (data -> strategy -> sizing -> execution
// -> portfolio -> analytics) with frictionless, zero-latency settings so that
// every number can be derived by hand, pinning down the accounting end to end.
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "quantforge/data_handler.hpp"
#include "quantforge/engine.hpp"
#include "quantforge/execution_handler.hpp"
#include "quantforge/execution_models.hpp"
#include "quantforge/strategy.hpp"
#include "quantforge/types.hpp"

using namespace qf;

namespace {

MarketEvent bar(Timestamp ts, Price close) {
    MarketEvent m;
    m.ts = ts;
    m.symbol = 0;
    m.kind = MarketKind::Bar;
    m.open = close;
    m.high = close;
    m.low = close;
    m.close = close;
    m.volume = 0.0;  // zero volume; execution configured to fill anyway
    return m;
}

// A strategy that emits a scripted signal on specific bar indices. Lets us pin
// exactly when the engine trades so the resulting PnL is hand-computable.
class ScriptedStrategy final : public Strategy {
  public:
    struct Entry {
        std::size_t bar_index;
        SignalDirection dir;
    };

    explicit ScriptedStrategy(std::vector<Entry> script)
        : script_(std::move(script)) {}

    void onMarket(const MarketEvent&, const StrategyContext& ctx) override {
        for (const auto& e : script_) {
            if (e.bar_index == idx_) {
                ctx.emitSignal(0, e.dir);
            }
        }
        ++idx_;
    }

    void reset() override { idx_ = 0; }
    std::string name() const override { return "ScriptedStrategy"; }

  private:
    std::vector<Entry> script_;
    std::size_t idx_{0};
};

// Frictionless execution: zero latency, zero slippage, zero fees, fills even
// with no volume -> fills land exactly at the bar close price.
ExecutionHandler frictionless() {
    ExecutionConfig cfg;
    cfg.fill_on_zero_volume = true;
    return ExecutionHandler(std::make_unique<FixedLatency>(0),
                            std::make_unique<FixedBpsSlippage>(0.0),
                            std::make_unique<PerShareFee>(0.0), cfg, 0);
}

}  // namespace

// Hand-computed scenario (see file header math):
//   cash0=100000, target=10 shares
//   bar0 close=10, Long  -> buy 10 @10  -> cash 99900, pos 10@10
//   bar1 close=20 (mark) -> equity 100100
//   bar2 close=15 (mark) -> equity 100050
//   bar3 close=25, Exit  -> sell 10@25  -> realized (25-10)*10=150, cash 100150
//   final equity 100150, realized 150
TEST(KnownAnswer, LongThenExitEndToEnd) {
    std::vector<MarketEvent> bars{bar(0, 10.0), bar(1, 20.0), bar(2, 15.0),
                                  bar(3, 25.0)};
    CsvBarDataHandler data(bars);
    ScriptedStrategy strat({{0, SignalDirection::Long}, {3, SignalDirection::Exit}});

    BacktestConfig cfg;
    cfg.initial_cash = 100'000.0;
    cfg.target_position = 10.0;

    Engine engine(data, strat, frictionless(), cfg);
    auto res = engine.run();

    EXPECT_EQ(res.num_fills, 2u);  // one entry, one exit
    EXPECT_DOUBLE_EQ(res.final_equity, 100'150.0);
    EXPECT_DOUBLE_EQ(res.final_cash, 100'150.0);
    EXPECT_DOUBLE_EQ(engine.portfolio().realizedPnl(), 150.0);

    // Equity curve: one point per bar at that bar's mark.
    ASSERT_EQ(res.equity_curve.size(), 4u);
    EXPECT_DOUBLE_EQ(res.equity_curve[0].equity, 99'900.0 + 10.0 * 10.0);  // 100000
    EXPECT_DOUBLE_EQ(res.equity_curve[1].equity, 100'100.0);
    EXPECT_DOUBLE_EQ(res.equity_curve[2].equity, 100'050.0);
    EXPECT_DOUBLE_EQ(res.equity_curve[3].equity, 100'150.0);
}

// A short scenario, also hand-computed:
//   bar0 close=50, Short -> sell 10 @50 -> cash 100500, pos -10@50
//   bar1 close=40, Exit  -> buy 10 @40  -> realized (50-40)*10=100, cash 100100
//   final equity 100100
TEST(KnownAnswer, ShortThenExitEndToEnd) {
    std::vector<MarketEvent> bars{bar(0, 50.0), bar(1, 40.0)};
    CsvBarDataHandler data(bars);
    ScriptedStrategy strat({{0, SignalDirection::Short}, {1, SignalDirection::Exit}});

    BacktestConfig cfg;
    cfg.initial_cash = 100'000.0;
    cfg.target_position = 10.0;

    Engine engine(data, strat, frictionless(), cfg);
    auto res = engine.run();

    EXPECT_EQ(res.num_fills, 2u);
    EXPECT_DOUBLE_EQ(res.final_equity, 100'100.0);
    EXPECT_DOUBLE_EQ(engine.portfolio().realizedPnl(), 100.0);
}

// With a 10 bps fee on each side and 5 bps slippage, verify the costed PnL.
//   target=100 shares, prices 100 then 110
//   bar0 Long: slippage buy -> 100*(1.0005)=100.05; qty 100
//            cost = 100.05*100 = 10005; fee 10bps notional = 100.05*100*0.001=10.005
//            cash = 100000 - 10005 - 10.005 = 89984.995; pos 100 @100.05
//   bar1 Exit (close 110): slippage sell -> 110*(0.9995)=109.945; qty 100
//            proceeds = 109.945*100 = 10994.5; fee = 109.945*100*0.001 = 10.9945
//            cash = 89984.995 + 10994.5 - 10.9945 = 100968.5005
//   final equity = cash (flat) = 100968.5005
TEST(KnownAnswer, CostedRoundTripEndToEnd) {
    std::vector<MarketEvent> bars{bar(0, 100.0), bar(1, 110.0)};
    CsvBarDataHandler data(bars);
    ScriptedStrategy strat({{0, SignalDirection::Long}, {1, SignalDirection::Exit}});

    BacktestConfig cfg;
    cfg.initial_cash = 100'000.0;
    cfg.target_position = 100.0;

    ExecutionConfig ecfg;
    ecfg.fill_on_zero_volume = true;
    ExecutionHandler exec(std::make_unique<FixedLatency>(0),
                          std::make_unique<FixedBpsSlippage>(5.0),
                          std::make_unique<BpsFee>(10.0), ecfg, 0);

    Engine engine(data, strat, std::move(exec), cfg);
    auto res = engine.run();

    EXPECT_EQ(res.num_fills, 2u);
    EXPECT_NEAR(res.final_equity, 100'968.5005, 1e-6);
}

// Engine-level no-lookahead: a strategy records the close it sees on each bar.
// Because the engine delivers bars strictly in order and signals are processed
// only after the market event that triggered them, the price a strategy acts on
// can never be a future bar's price. We assert the strategy only ever observes
// closes up to and including the current bar.
TEST(KnownAnswer, EngineNeverLeaksFuturePrices) {
    std::vector<MarketEvent> bars{bar(0, 10.0), bar(1, 11.0), bar(2, 12.0),
                                  bar(3, 13.0), bar(4, 14.0)};

    // Recording strategy: capture (index, close) seen on each onMarket call.
    class Recorder final : public Strategy {
      public:
        void onMarket(const MarketEvent& m, const StrategyContext&) override {
            seen.push_back(m.close);
        }
        void reset() override { seen.clear(); }
        std::string name() const override { return "Recorder"; }
        std::vector<Price> seen;
    };

    CsvBarDataHandler data(bars);
    Recorder rec;
    BacktestConfig cfg;
    Engine engine(data, rec, frictionless(), cfg);
    engine.run();

    ASSERT_EQ(rec.seen.size(), bars.size());
    // The i-th observation must equal the i-th bar's close -- never a later one.
    for (std::size_t i = 0; i < bars.size(); ++i) {
        EXPECT_DOUBLE_EQ(rec.seen[i], bars[i].close);
    }
}
