#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "quantforge/analytics.hpp"
#include "quantforge/data_handler.hpp"
#include "quantforge/event.hpp"
#include "quantforge/event_queue.hpp"
#include "quantforge/execution_handler.hpp"
#include "quantforge/portfolio.hpp"
#include "quantforge/strategy.hpp"
#include "quantforge/types.hpp"

namespace qf {

/// Configuration for a single backtest run.
struct BacktestConfig {
    double initial_cash{100'000.0};
    RiskLimits risk{};
    ExecutionConfig execution{};

    /// Target absolute position size (units/shares) a Long/Short signal aims
    /// for. Exit targets zero. Orders are sized as the delta to this target,
    /// then clamped by risk limits.
    Quantity target_position{100.0};

    /// Annualization factor for risk metrics (252 for daily bars).
    double periods_per_year{kTradingDaysPerYear};

    /// RNG seed; drives the execution handler so runs are reproducible.
    std::uint64_t seed{0};

    /// If true, record an equity-curve point at each distinct market timestamp.
    bool record_equity_curve{true};
};

/// Result of a backtest run: performance report plus the raw series needed for
/// plotting and further analysis in Python.
struct BacktestResult {
    PerformanceReport report{};
    std::vector<EquityPoint> equity_curve;
    std::vector<TradeRecord> trades;

    double final_equity{0.0};
    double final_cash{0.0};
    double total_commission{0.0};
    std::size_t num_fills{0};
    std::size_t num_signals{0};
    std::size_t num_orders{0};
    std::size_t num_market_events{0};
};

/// The event-driven backtest engine. It owns no data or strategy lifetime
/// (both are injected by reference) but drives them through a single
/// time-ordered event queue, guaranteeing the Market -> Signal -> Order -> Fill
/// causal order at every timestamp.
class Engine {
  public:
    Engine(DataHandler& data, Strategy& strategy, ExecutionHandler execution,
           BacktestConfig cfg);

    /// Runs the full backtest and returns the result. Resets strategy,
    /// portfolio, data cursor and RNG first, so the same Engine can be re-run
    /// deterministically.
    BacktestResult run();

    const Portfolio& portfolio() const noexcept { return portfolio_; }

  private:
    void handleMarket(const MarketEvent& m);
    void handleSignal(const SignalEvent& s);
    void handleOrder(const OrderEvent& o);
    void handleFill(const FillEvent& f);
    void recordEquity(Timestamp ts);

    DataHandler& data_;
    Strategy& strategy_;
    ExecutionHandler execution_;
    Portfolio portfolio_;
    BacktestConfig cfg_;

    EventQueue queue_;

    // Latest market snapshot per symbol, used to price executions and marks.
    std::unordered_map<SymbolId, MarketSnapshot> snapshots_;

    std::uint64_t next_order_id_{1};
    Timestamp last_equity_ts_{std::numeric_limits<Timestamp>::min()};

    BacktestResult result_;
};

}  // namespace qf
