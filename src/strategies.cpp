#include "quantforge/strategies.hpp"

#include <cmath>
#include <stdexcept>

namespace qf {

// ---------------------------------------------------------------------------
// MovingAverageCrossover
// ---------------------------------------------------------------------------
MovingAverageCrossover::MovingAverageCrossover(SymbolId symbol, std::size_t fast, std::size_t slow)
    : symbol_(symbol), fast_(fast), slow_(slow) {
    if (fast == 0 || slow == 0 || fast >= slow) {
        throw std::invalid_argument("MovingAverageCrossover requires 0 < fast < slow");
    }
}

void MovingAverageCrossover::onMarket(const MarketEvent& ev, const StrategyContext& ctx) {
    if (ev.symbol != symbol_) {
        return;
    }
    const double px = ev.referencePrice();
    fast_.push(px);
    slow_.push(px);

    // Wait until the slow window has enough data to be meaningful.
    if (!slow_.full()) {
        return;
    }

    const double f = fast_.mean();
    const double s = slow_.mean();
    const int state = f > s ? 1 : (f < s ? -1 : last_state_);

    if (state != last_state_) {
        if (state > 0) {
            ctx.emitSignal(symbol_, SignalDirection::Long);
        } else if (state < 0) {
            ctx.emitSignal(symbol_, SignalDirection::Short);
        }
        last_state_ = state;
    }
}

void MovingAverageCrossover::reset() {
    fast_.clear();
    slow_.clear();
    last_state_ = 0;
}

std::string MovingAverageCrossover::name() const {
    return "MovingAverageCrossover";
}

// ---------------------------------------------------------------------------
// MeanReversion
// ---------------------------------------------------------------------------
MeanReversion::MeanReversion(SymbolId symbol, std::size_t lookback, double entry_z, double exit_z)
    : symbol_(symbol), window_(lookback), entry_z_(entry_z), exit_z_(exit_z) {
    if (entry_z <= 0.0 || exit_z < 0.0 || exit_z >= entry_z) {
        throw std::invalid_argument("MeanReversion requires 0 <= exit_z < entry_z and entry_z > 0");
    }
}

void MeanReversion::onMarket(const MarketEvent& ev, const StrategyContext& ctx) {
    if (ev.symbol != symbol_) {
        return;
    }
    const double px = ev.referencePrice();
    window_.push(px);
    if (!window_.full()) {
        return;
    }

    const double mu = window_.mean();
    const double sd = window_.stdev();
    if (sd <= 0.0) {
        return;  // no dispersion -> no signal
    }
    const double z = (px - mu) / sd;

    if (position_ == 0) {
        if (z >= entry_z_) {
            ctx.emitSignal(symbol_, SignalDirection::Short);
            position_ = -1;
        } else if (z <= -entry_z_) {
            ctx.emitSignal(symbol_, SignalDirection::Long);
            position_ = 1;
        }
    } else if (std::abs(z) <= exit_z_) {
        ctx.emitSignal(symbol_, SignalDirection::Exit);
        position_ = 0;
    }
}

void MeanReversion::reset() {
    window_.clear();
    position_ = 0;
}

std::string MeanReversion::name() const {
    return "MeanReversion";
}

// ---------------------------------------------------------------------------
// MarketMaker
// ---------------------------------------------------------------------------
MarketMaker::MarketMaker(SymbolId symbol, std::size_t fair_lookback, double band,
                         double max_inventory)
    : symbol_(symbol), fair_(fair_lookback), band_(band), max_inventory_(max_inventory) {
    if (band <= 0.0 || max_inventory <= 0.0) {
        throw std::invalid_argument("MarketMaker requires band > 0 and max_inventory > 0");
    }
}

void MarketMaker::onMarket(const MarketEvent& ev, const StrategyContext& ctx) {
    if (ev.symbol != symbol_) {
        return;
    }
    const double mid = ev.referencePrice();
    fair_.push(mid);
    if (!fair_.full()) {
        return;
    }

    const double fair = fair_.mean();
    if (fair <= 0.0) {
        return;
    }
    const double dev = (mid - fair) / fair;  // fractional deviation from fair

    // Lean against the deviation (buy below fair, sell above), but respect the
    // conceptual inventory cap: once long near the cap, stop adding longs.
    int state = last_state_;
    if (dev <= -band_ && inventory_ < max_inventory_) {
        state = 1;  // buy the dip
    } else if (dev >= band_ && inventory_ > -max_inventory_) {
        state = -1;  // sell the rip
    } else if (std::abs(dev) < band_ * 0.25) {
        state = 0;  // close to fair: flatten quoting bias
    }

    if (state != last_state_) {
        switch (state) {
            case 1:
                ctx.emitSignal(symbol_, SignalDirection::Long);
                inventory_ += 1.0;
                break;
            case -1:
                ctx.emitSignal(symbol_, SignalDirection::Short);
                inventory_ -= 1.0;
                break;
            default:
                ctx.emitSignal(symbol_, SignalDirection::Exit);
                inventory_ = 0.0;
                break;
        }
        last_state_ = state;
    }
}

void MarketMaker::reset() {
    fair_.clear();
    inventory_ = 0.0;
    last_state_ = 0;
}

std::string MarketMaker::name() const {
    return "MarketMaker";
}

}  // namespace qf
