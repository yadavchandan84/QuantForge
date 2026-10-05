#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <stdexcept>

#include "quantforge/event.hpp"
#include "quantforge/types.hpp"

namespace qf {

// ===========================================================================
// Policy-based execution models.
//
// Each model family is expressed as an abstract policy with concrete
// implementations, so an ExecutionHandler can be composed from any combination
// at runtime (useful for Python and parameter sweeps). The hot-path methods are
// small and branch-light. Randomized policies draw from a caller-supplied RNG
// (std::mt19937_64&) so results are fully determined by the engine's seed.
// ===========================================================================

/// Latency model: how long after submission an order reaches the market, in
/// nanoseconds. Added to the order timestamp to schedule the fill.
class LatencyModel {
  public:
    virtual ~LatencyModel() = default;
    virtual Timestamp sample(std::mt19937_64& rng) const = 0;
    virtual std::unique_ptr<LatencyModel> clone() const = 0;
};

/// Constant latency.
class FixedLatency final : public LatencyModel {
  public:
    explicit FixedLatency(Timestamp ns) : ns_(ns) {
        if (ns < 0)
            throw std::invalid_argument("FixedLatency must be >= 0");
    }
    Timestamp sample(std::mt19937_64&) const override { return ns_; }
    std::unique_ptr<LatencyModel> clone() const override {
        return std::make_unique<FixedLatency>(*this);
    }

  private:
    Timestamp ns_;
};

/// Uniform random latency in [min_ns, max_ns].
class RandomLatency final : public LatencyModel {
  public:
    RandomLatency(Timestamp min_ns, Timestamp max_ns) : min_(min_ns), max_(max_ns) {
        if (min_ns < 0 || max_ns < min_ns) {
            throw std::invalid_argument("RandomLatency requires 0 <= min <= max");
        }
    }
    Timestamp sample(std::mt19937_64& rng) const override {
        std::uniform_int_distribution<Timestamp> dist(min_, max_);
        return dist(rng);
    }
    std::unique_ptr<LatencyModel> clone() const override {
        return std::make_unique<RandomLatency>(*this);
    }

  private:
    Timestamp min_;
    Timestamp max_;
};

/// Slippage model: adjusts the execution price adversely to the taker. Returns
/// the fill price given the reference price, side, and the quantity filled
/// relative to available volume.
class SlippageModel {
  public:
    virtual ~SlippageModel() = default;
    /// Returns the fill price. `rng` is the handler's single RNG stream; models
    /// that need randomness draw from it so replay is reproducible, while
    /// deterministic models simply ignore it.
    virtual Price apply(Price reference, Side side, Quantity fill_qty, Quantity available_volume,
                        std::mt19937_64& rng) const = 0;
    virtual std::unique_ptr<SlippageModel> clone() const = 0;
};

/// Fixed slippage in basis points, always against the taker.
class FixedBpsSlippage final : public SlippageModel {
  public:
    explicit FixedBpsSlippage(double bps) : bps_(bps) {
        if (bps < 0.0)
            throw std::invalid_argument("slippage bps must be >= 0");
    }
    Price apply(Price reference, Side side, Quantity, Quantity, std::mt19937_64&) const override {
        const double frac = bps_ * 1e-4;
        return side == Side::Buy ? reference * (1.0 + frac) : reference * (1.0 - frac);
    }
    std::unique_ptr<SlippageModel> clone() const override {
        return std::make_unique<FixedBpsSlippage>(*this);
    }

  private:
    double bps_;
};

/// Randomized slippage: draws an adverse move uniformly in [min_bps, max_bps].
/// Because it consumes the RNG, it is the sharpest test of deterministic
/// replay: identical seeds must reproduce identical fill prices.
class RandomBpsSlippage final : public SlippageModel {
  public:
    RandomBpsSlippage(double min_bps, double max_bps) : min_bps_(min_bps), max_bps_(max_bps) {
        if (min_bps < 0.0 || max_bps < min_bps) {
            throw std::invalid_argument("RandomBpsSlippage requires 0 <= min <= max");
        }
    }
    Price apply(Price reference, Side side, Quantity, Quantity,
                std::mt19937_64& rng) const override {
        std::uniform_real_distribution<double> dist(min_bps_, max_bps_);
        const double frac = dist(rng) * 1e-4;
        return side == Side::Buy ? reference * (1.0 + frac) : reference * (1.0 - frac);
    }
    std::unique_ptr<SlippageModel> clone() const override {
        return std::make_unique<RandomBpsSlippage>(*this);
    }

  private:
    double min_bps_;
    double max_bps_;
};

/// Volume-based (square-root market impact) slippage. The adverse move scales
/// with the fraction of available volume consumed:
///     impact_bps = coeff_bps * sqrt(fill_qty / available_volume)
/// This captures the intuition that larger orders relative to liquidity pay
/// more. Falls back to coeff_bps when volume is unavailable.
class VolumeSlippage final : public SlippageModel {
  public:
    explicit VolumeSlippage(double coeff_bps) : coeff_bps_(coeff_bps) {
        if (coeff_bps < 0.0)
            throw std::invalid_argument("slippage coeff must be >= 0");
    }
    Price apply(Price reference, Side side, Quantity fill_qty, Quantity available_volume,
                std::mt19937_64&) const override {
        double participation = 1.0;
        if (available_volume > 0.0) {
            participation = std::clamp(fill_qty / available_volume, 0.0, 1.0);
        }
        const double impact_bps = coeff_bps_ * std::sqrt(participation);
        const double frac = impact_bps * 1e-4;
        return side == Side::Buy ? reference * (1.0 + frac) : reference * (1.0 - frac);
    }
    std::unique_ptr<SlippageModel> clone() const override {
        return std::make_unique<VolumeSlippage>(*this);
    }

  private:
    double coeff_bps_;
};

/// Fee model: commission charged on a fill, in account currency.
class FeeModel {
  public:
    virtual ~FeeModel() = default;
    virtual double commission(Price fill_price, Quantity qty) const = 0;
    virtual std::unique_ptr<FeeModel> clone() const = 0;
};

/// Per-share (per-unit) commission.
class PerShareFee final : public FeeModel {
  public:
    explicit PerShareFee(double fee_per_share) : fee_(fee_per_share) {
        if (fee_per_share < 0.0)
            throw std::invalid_argument("per-share fee must be >= 0");
    }
    double commission(Price, Quantity qty) const override { return fee_ * std::abs(qty); }
    std::unique_ptr<FeeModel> clone() const override {
        return std::make_unique<PerShareFee>(*this);
    }

  private:
    double fee_;
};

/// Basis-points-of-notional commission.
class BpsFee final : public FeeModel {
  public:
    explicit BpsFee(double bps) : bps_(bps) {
        if (bps < 0.0)
            throw std::invalid_argument("fee bps must be >= 0");
    }
    double commission(Price fill_price, Quantity qty) const override {
        return fill_price * std::abs(qty) * bps_ * 1e-4;
    }
    std::unique_ptr<FeeModel> clone() const override { return std::make_unique<BpsFee>(*this); }

  private:
    double bps_;
};

}  // namespace qf
