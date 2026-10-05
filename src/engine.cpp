#include "quantforge/engine.hpp"

#include <cmath>
#include <limits>

namespace qf {

Engine::Engine(DataHandler& data, Strategy& strategy, ExecutionHandler execution,
               BacktestConfig cfg)
    : data_(data),
      strategy_(strategy),
      execution_(std::move(execution)),
      portfolio_(cfg.initial_cash, cfg.risk),
      cfg_(cfg) {}

BacktestResult Engine::run() {
    // Deterministic fresh start.
    data_.reset();
    strategy_.reset();
    portfolio_.reset();
    execution_.reseed(cfg_.seed);
    queue_.clear();
    snapshots_.clear();
    next_order_id_ = 1;
    last_equity_ts_ = std::numeric_limits<Timestamp>::min();
    result_ = BacktestResult{};

    // Drive the simulation. We pull one market event at a time from the data
    // source and fully drain the queue it generates before pulling the next,
    // which keeps the simulation clock monotonic and bounds queue growth.
    while (!data_.finished()) {
        auto md = data_.next();
        if (!md) {
            break;
        }
        queue_.emit(*md);

        while (auto ev = queue_.pop()) {
            std::visit(
                [this](auto&& payload) {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<T, MarketEvent>) {
                        handleMarket(payload);
                    } else if constexpr (std::is_same_v<T, SignalEvent>) {
                        handleSignal(payload);
                    } else if constexpr (std::is_same_v<T, OrderEvent>) {
                        handleOrder(payload);
                    } else if constexpr (std::is_same_v<T, FillEvent>) {
                        handleFill(payload);
                    }
                },
                ev->payload);
        }
    }

    // Finalize results.
    result_.final_equity = portfolio_.equity();
    result_.final_cash = portfolio_.cash();
    result_.total_commission = portfolio_.totalCommission();
    result_.report =
        computeReport(result_.equity_curve, result_.trades, cfg_.periods_per_year);
    return result_;
}

void Engine::handleMarket(const MarketEvent& m) {
    ++result_.num_market_events;

    // Update the snapshot used for pricing executions and marks.
    MarketSnapshot& snap = snapshots_[m.symbol];
    snap.reference = m.referencePrice();
    snap.volume = m.volume;
    snap.bid = m.bid > 0.0 ? m.bid : m.referencePrice();
    snap.ask = m.ask > 0.0 ? m.ask : m.referencePrice();

    // Mark the portfolio to the new price, then record equity for this instant.
    portfolio_.onMark(m.symbol, snap.reference);
    recordEquity(m.ts);

    // Feed the strategy. Signals it emits are pushed onto the queue (at Signal
    // priority) so they are processed after all market events at this instant.
    StrategyContext ctx(
        m.ts, [this](const SignalEvent& s) { queue_.emit(s); }, strategy_.id());
    strategy_.onMarket(m, ctx);
}

void Engine::handleSignal(const SignalEvent& s) {
    ++result_.num_signals;

    // Target-position sizing: translate the directional signal into a target
    // absolute position, then order the delta from the current position.
    const double current = portfolio_.position(s.symbol).quantity;
    double target = 0.0;
    switch (s.direction) {
        case SignalDirection::Long:
            target = cfg_.target_position * s.strength;
            break;
        case SignalDirection::Short:
            target = -cfg_.target_position * s.strength;
            break;
        case SignalDirection::Exit:
            target = 0.0;
            break;
    }

    const double delta = target - current;
    if (delta == 0.0) {
        return;
    }

    const Side side = delta > 0.0 ? Side::Buy : Side::Sell;
    const Quantity desired = std::abs(delta);

    // Price used for risk clamping is the latest known reference.
    Price ref = 0.0;
    if (auto it = snapshots_.find(s.symbol); it != snapshots_.end()) {
        ref = it->second.reference;
    }
    const Quantity sized =
        portfolio_.clampOrderQuantity(s.symbol, side, desired, ref);
    if (sized <= 0.0) {
        return;
    }

    OrderEvent o;
    o.ts = s.ts;
    o.symbol = s.symbol;
    o.side = side;
    o.type = OrderType::Market;
    o.quantity = sized;
    o.order_id = next_order_id_++;
    queue_.emit(o);
}

void Engine::handleOrder(const OrderEvent& o) {
    ++result_.num_orders;

    auto it = snapshots_.find(o.symbol);
    if (it == snapshots_.end()) {
        return;  // no market seen yet; cannot price
    }
    if (auto fill = execution_.execute(o, it->second)) {
        queue_.emit(*fill);
    }
}

void Engine::handleFill(const FillEvent& f) {
    ++result_.num_fills;

    // Capture realized PnL delta attributable to this fill (for trade stats).
    const double realized_before = portfolio_.position(f.symbol).realized_pnl;
    portfolio_.onFill(f);
    const double realized_after = portfolio_.position(f.symbol).realized_pnl;

    TradeRecord tr;
    tr.ts = f.ts;
    tr.symbol = f.symbol;
    tr.realized_pnl = realized_after - realized_before;
    tr.notional = std::abs(f.fill_price * f.quantity);
    result_.trades.push_back(tr);
}

void Engine::recordEquity(Timestamp ts) {
    if (!cfg_.record_equity_curve) {
        return;
    }
    // One point per distinct timestamp. If multiple symbols share a timestamp,
    // the last mark at that instant updates the same point.
    if (ts == last_equity_ts_ && !result_.equity_curve.empty()) {
        result_.equity_curve.back().equity = portfolio_.equity();
        return;
    }
    result_.equity_curve.push_back(EquityPoint{ts, portfolio_.equity()});
    last_equity_ts_ = ts;
}

}  // namespace qf
