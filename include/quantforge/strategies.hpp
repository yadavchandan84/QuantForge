#pragma once

#include <cstddef>
#include <string>

#include "quantforge/rolling.hpp"
#include "quantforge/strategy.hpp"
#include "quantforge/types.hpp"

namespace qf {

/// Classic trend-following strategy: go long when a fast moving average crosses
/// above a slow one, go short on the opposite cross. Emits a signal only on the
/// bar where the cross occurs (state change), not every bar.
class MovingAverageCrossover final : public Strategy {
  public:
    MovingAverageCrossover(SymbolId symbol, std::size_t fast, std::size_t slow);

    void onMarket(const MarketEvent& ev, const StrategyContext& ctx) override;
    void reset() override;
    std::string name() const override;

  private:
    SymbolId symbol_;
    RollingWindow fast_;
    RollingWindow slow_;
    int last_state_{0};  ///< -1 short, 0 flat, +1 long.
};

/// Single-instrument mean reversion. Maintains a rolling window of closes and
/// trades the z-score: short when price is `entry_z` std-devs above the mean,
/// long when `entry_z` below, and exits when it reverts within `exit_z`.
class MeanReversion final : public Strategy {
  public:
    MeanReversion(SymbolId symbol, std::size_t lookback, double entry_z, double exit_z);

    void onMarket(const MarketEvent& ev, const StrategyContext& ctx) override;
    void reset() override;
    std::string name() const override;

  private:
    SymbolId symbol_;
    RollingWindow window_;
    double entry_z_;
    double exit_z_;
    int position_{0};  ///< -1 short, 0 flat, +1 long.
};

/// Minimal inventory-aware market maker for L1 quote data. It does not post
/// resting orders (the simplified execution model fills marketable orders);
/// instead it emits a mean-reversion-style signal around the mid, skewed by
/// current inventory, to demonstrate quoting logic within the event framework.
///
/// When the mid moves `band` fraction below the recent fair value it leans long
/// (buy the dip it is "quoting"); above, it leans short; and it flattens when
/// inventory would exceed `max_inventory` conceptual units.
class MarketMaker final : public Strategy {
  public:
    MarketMaker(SymbolId symbol, std::size_t fair_lookback, double band, double max_inventory);

    void onMarket(const MarketEvent& ev, const StrategyContext& ctx) override;
    void reset() override;
    std::string name() const override;

  private:
    SymbolId symbol_;
    RollingWindow fair_;
    double band_;
    double max_inventory_;
    double inventory_{0.0};
    int last_state_{0};
};

}  // namespace qf
