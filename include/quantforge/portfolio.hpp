#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "quantforge/event.hpp"
#include "quantforge/types.hpp"

namespace qf {

/// A single-instrument position tracked with average-cost accounting.
///
/// Sign convention: `quantity` is positive for a long position and negative for
/// a short. `avg_price` is the average entry price of the currently open
/// quantity. Realized PnL accrues as quantity is reduced or crossed.
struct Position {
    Quantity quantity{0.0};
    Price avg_price{0.0};
    Price last_price{0.0};   ///< Most recent mark price.
    double realized_pnl{0.0};
    double total_commission{0.0};

    bool isFlat() const noexcept { return quantity == 0.0; }

    /// Unrealized PnL at the current mark: (mark - avg) * quantity.
    double unrealizedPnl() const noexcept {
        return (last_price - avg_price) * quantity;
    }

    /// Signed market value of the position at the current mark.
    double marketValue() const noexcept { return last_price * quantity; }
};

/// Risk limits enforced by the portfolio when sizing/clamping orders.
struct RiskLimits {
    /// Maximum absolute position size per symbol (in units/shares). 0 disables.
    Quantity max_position{0.0};
    /// Maximum gross exposure across all symbols (sum of |market value|). 0
    /// disables.
    double max_gross_exposure{0.0};

    bool hasPositionLimit() const noexcept { return max_position > 0.0; }
    bool hasExposureLimit() const noexcept { return max_gross_exposure > 0.0; }
};

/// Tracks cash, positions, PnL and enforces risk limits for a backtest run.
///
/// Determinism: the portfolio holds no randomness and processes fills in the
/// order the engine delivers them, so given the same event stream it always
/// produces the same equity path.
class Portfolio {
  public:
    explicit Portfolio(double initial_cash, RiskLimits limits = {});

    /// Applies a fill: updates cash, position, avg cost and realized PnL.
    void onFill(const FillEvent& fill);

    /// Marks a symbol to the latest price (does not change cash/position).
    void onMark(SymbolId symbol, Price price);

    /// Convenience: mark from a market event's reference price.
    void onMarket(const MarketEvent& ev) { onMark(ev.symbol, ev.referencePrice()); }

    // ---- Accessors -------------------------------------------------------
    double cash() const noexcept { return cash_; }
    double initialCash() const noexcept { return initial_cash_; }

    /// Total account equity = cash + sum of position market values.
    double equity() const;

    /// Sum of |market value| across all positions.
    double grossExposure() const;

    /// Realized + unrealized PnL across all positions (excludes starting cash).
    double totalPnl() const;

    double realizedPnl() const;
    double unrealizedPnl() const;
    double totalCommission() const;

    const Position& position(SymbolId symbol) const;
    bool hasPosition(SymbolId symbol) const;
    const std::unordered_map<SymbolId, Position>& positions() const noexcept {
        return positions_;
    }

    const RiskLimits& limits() const noexcept { return limits_; }

    /// Clamps a desired order quantity for `symbol`/`side` so that filling it
    /// would not breach the per-symbol position limit or gross exposure limit,
    /// given the current `price`. Returns the (possibly reduced) quantity, which
    /// may be 0 if the order must be fully rejected.
    Quantity clampOrderQuantity(SymbolId symbol, Side side, Quantity desired,
                                Price price) const;

    void reset();

  private:
    Position& mutablePosition(SymbolId symbol);

    double initial_cash_;
    double cash_;
    RiskLimits limits_;
    std::unordered_map<SymbolId, Position> positions_;
    static const Position kFlat;
};

}  // namespace qf
