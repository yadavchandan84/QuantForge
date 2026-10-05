#include "quantforge/portfolio.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace qf {

const Position Portfolio::kFlat{};

Portfolio::Portfolio(double initial_cash, RiskLimits limits)
    : initial_cash_(initial_cash), cash_(initial_cash), limits_(limits) {
    if (initial_cash < 0.0) {
        throw std::invalid_argument("initial cash must be >= 0");
    }
    if (limits_.max_position < 0.0 || limits_.max_gross_exposure < 0.0) {
        throw std::invalid_argument("risk limits must be >= 0");
    }
}

Position& Portfolio::mutablePosition(SymbolId symbol) {
    return positions_[symbol];
}

void Portfolio::onFill(const FillEvent& fill) {
    Position& pos = mutablePosition(fill.symbol);

    // Signed traded quantity: + for buys, - for sells.
    const double signed_qty = (fill.side == Side::Buy ? 1.0 : -1.0) * fill.quantity;

    // Cash: buying costs cash, selling adds cash; commission always reduces.
    cash_ -= fill.fill_price * signed_qty;
    cash_ -= fill.commission;
    pos.total_commission += fill.commission;

    const double old_qty = pos.quantity;
    const double new_qty = old_qty + signed_qty;

    if (old_qty == 0.0) {
        // Opening a fresh position.
        pos.avg_price = fill.fill_price;
    } else if ((old_qty > 0.0) == (signed_qty > 0.0)) {
        // Increasing an existing position on the same side: blend avg cost.
        const double total_cost = pos.avg_price * old_qty + fill.fill_price * signed_qty;
        pos.avg_price = total_cost / new_qty;
    } else {
        // Reducing, closing, or crossing through zero.
        const double closing_qty = std::min(std::abs(signed_qty), std::abs(old_qty));
        // PnL on the closed portion: (exit - entry) * closed, signed by the
        // direction of the position being closed.
        const double dir = old_qty > 0.0 ? 1.0 : -1.0;
        pos.realized_pnl += (fill.fill_price - pos.avg_price) * closing_qty * dir;

        if (std::abs(signed_qty) > std::abs(old_qty)) {
            // Crossed through zero: remainder opens a new position on the
            // opposite side at the fill price.
            pos.avg_price = fill.fill_price;
        } else if (new_qty == 0.0) {
            pos.avg_price = 0.0;  // fully flat
        }
        // If merely reduced (same side remains), avg_price is unchanged.
    }

    pos.quantity = new_qty;
    pos.last_price = fill.fill_price;  // most recent trade price
}

void Portfolio::onMark(SymbolId symbol, Price price) {
    if (auto it = positions_.find(symbol); it != positions_.end()) {
        it->second.last_price = price;
    }
}

double Portfolio::equity() const {
    double eq = cash_;
    for (const auto& [sym, pos] : positions_) {
        eq += pos.marketValue();
    }
    return eq;
}

double Portfolio::grossExposure() const {
    double gross = 0.0;
    for (const auto& [sym, pos] : positions_) {
        gross += std::abs(pos.marketValue());
    }
    return gross;
}

double Portfolio::totalPnl() const {
    return equity() - initial_cash_;
}

double Portfolio::realizedPnl() const {
    double r = 0.0;
    for (const auto& [sym, pos] : positions_) {
        r += pos.realized_pnl;
    }
    return r;
}

double Portfolio::unrealizedPnl() const {
    double u = 0.0;
    for (const auto& [sym, pos] : positions_) {
        u += pos.unrealizedPnl();
    }
    return u;
}

double Portfolio::totalCommission() const {
    double c = 0.0;
    for (const auto& [sym, pos] : positions_) {
        c += pos.total_commission;
    }
    return c;
}

const Position& Portfolio::position(SymbolId symbol) const {
    if (auto it = positions_.find(symbol); it != positions_.end()) {
        return it->second;
    }
    return kFlat;
}

bool Portfolio::hasPosition(SymbolId symbol) const {
    return positions_.find(symbol) != positions_.end();
}

Quantity Portfolio::clampOrderQuantity(SymbolId symbol, Side side, Quantity desired,
                                       Price price) const {
    if (desired <= 0.0) {
        return 0.0;
    }
    Quantity allowed = desired;
    const double signed_sign = (side == Side::Buy ? 1.0 : -1.0);
    const Position& pos = position(symbol);

    // Per-symbol position limit: resulting |quantity| must not exceed the cap.
    if (limits_.hasPositionLimit()) {
        const double projected = pos.quantity + signed_sign * desired;
        if (std::abs(projected) > limits_.max_position) {
            // Reduce so that |pos.quantity + sign*allowed| == max_position.
            const double room = limits_.max_position - signed_sign * pos.quantity;
            // room is the signed capacity in the order's direction.
            allowed = std::min(allowed, std::max(0.0, room));
        }
    }

    // Gross exposure limit: adding this order must not push total gross over
    // the cap. Exposure contribution of the new quantity is |allowed * price|.
    if (limits_.hasExposureLimit() && price > 0.0) {
        const double current_gross = grossExposure();
        // Opening/increasing adds exposure; conservatively treat the new order
        // as additive to gross (a reduction that nets down is still bounded by
        // this check since allowed only shrinks).
        const double headroom = limits_.max_gross_exposure - current_gross;
        if (headroom <= 0.0) {
            // Only allow orders that reduce the existing position.
            const bool reduces = (pos.quantity > 0.0 && side == Side::Sell) ||
                                 (pos.quantity < 0.0 && side == Side::Buy);
            if (!reduces) {
                return 0.0;
            }
        } else {
            const Quantity max_by_exposure = headroom / price;
            allowed = std::min(allowed, max_by_exposure);
        }
    }

    return std::max(0.0, allowed);
}

void Portfolio::reset() {
    cash_ = initial_cash_;
    positions_.clear();
}

}  // namespace qf
