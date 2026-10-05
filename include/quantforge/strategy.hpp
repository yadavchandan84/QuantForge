#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "quantforge/event.hpp"
#include "quantforge/types.hpp"

namespace qf {

/// Context handed to a strategy on each market event. It is the strategy's only
/// outward channel: it can read the triggering event and emit signals, but it
/// has no access to future data, the order book of other venues, or the
/// portfolio internals. This keeps strategies decoupled and lookahead-free.
class StrategyContext {
  public:
    using SignalSink = std::function<void(const SignalEvent&)>;

    StrategyContext(Timestamp now, SignalSink sink, std::uint64_t strategy_id)
        : now_(now), sink_(std::move(sink)), strategy_id_(strategy_id) {}

    Timestamp now() const noexcept { return now_; }

    /// Emits a directional signal for `symbol`. `strength` scales sizing.
    void emitSignal(SymbolId symbol, SignalDirection dir, double strength = 1.0) const {
        SignalEvent s;
        s.ts = now_;
        s.symbol = symbol;
        s.direction = dir;
        s.strength = strength;
        s.strategy_id = strategy_id_;
        sink_(s);
    }

  private:
    Timestamp now_;
    SignalSink sink_;
    std::uint64_t strategy_id_;
};

/// Abstract strategy. Implement onMarket to react to each market event and emit
/// signals via the context. Strategies are stateful across the run; reset() is
/// called before each (re)run so the same instance can be reused by sweeps.
class Strategy {
  public:
    virtual ~Strategy() = default;

    /// Called once per market event, in strict time order.
    virtual void onMarket(const MarketEvent& ev, const StrategyContext& ctx) = 0;

    /// Clears internal state so the strategy can be replayed from scratch.
    virtual void reset() {}

    /// Human-readable name for logging/reporting.
    virtual std::string name() const = 0;

    std::uint64_t id() const noexcept { return id_; }
    void setId(std::uint64_t id) noexcept { id_ = id; }

  private:
    std::uint64_t id_{0};
};

}  // namespace qf
