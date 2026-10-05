#include "quantforge/execution_handler.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace qf {

ExecutionHandler::ExecutionHandler(std::unique_ptr<LatencyModel> latency,
                                   std::unique_ptr<SlippageModel> slippage,
                                   std::unique_ptr<FeeModel> fee, ExecutionConfig cfg,
                                   std::uint64_t seed)
    : latency_(std::move(latency)),
      slippage_(std::move(slippage)),
      fee_(std::move(fee)),
      cfg_(cfg),
      seed_(seed),
      rng_(seed) {
    if (!latency_ || !slippage_ || !fee_) {
        throw std::invalid_argument("ExecutionHandler requires all three models");
    }
    if (cfg_.max_participation <= 0.0 || cfg_.max_participation > 1.0) {
        throw std::invalid_argument("max_participation must be in (0, 1]");
    }
}

ExecutionHandler ExecutionHandler::clone(std::uint64_t seed) const {
    return ExecutionHandler(latency_->clone(), slippage_->clone(), fee_->clone(), cfg_, seed);
}

void ExecutionHandler::reseed(std::uint64_t seed) {
    seed_ = seed;
    rng_.seed(seed);
}

std::optional<FillEvent> ExecutionHandler::execute(const OrderEvent& order,
                                                   const MarketSnapshot& snap) {
    if (order.quantity <= 0.0) {
        return std::nullopt;
    }

    // Determine the base reference price. For a limit order, require that the
    // market is marketable against the limit; otherwise it does not fill in
    // this simplified model (no resting book).
    Price reference = snap.reference;
    if (order.type == OrderType::Limit) {
        if (order.side == Side::Buy) {
            // Buy limit fills only if the market trades at or below the limit.
            if (reference > order.limit_price) {
                return std::nullopt;
            }
            // Price improvement: fill at the better of limit/reference.
            reference = std::min(reference, order.limit_price);
        } else {
            if (reference < order.limit_price) {
                return std::nullopt;
            }
            reference = std::max(reference, order.limit_price);
        }
    }

    // Partial fills against available volume.
    Quantity fill_qty = order.quantity;
    const Quantity vol = snap.volume;
    if (vol > 0.0) {
        const Quantity max_fill = cfg_.max_participation * vol;
        fill_qty = std::min(order.quantity, max_fill);
    } else if (!cfg_.fill_on_zero_volume) {
        return std::nullopt;
    }
    if (fill_qty <= 0.0) {
        return std::nullopt;
    }

    // Slippage against the taker. Draw slippage before latency so the RNG
    // consumption order is fixed and replay-stable.
    const Price fill_price = slippage_->apply(reference, order.side, fill_qty, vol, rng_);

    // Latency determines when the fill is observed.
    const Timestamp lat = latency_->sample(rng_);
    const double commission = fee_->commission(fill_price, fill_qty);

    FillEvent f;
    f.ts = order.ts + lat;
    f.symbol = order.symbol;
    f.side = order.side;
    f.quantity = fill_qty;
    f.fill_price = fill_price;
    f.commission = commission;
    f.remaining = order.quantity - fill_qty;
    f.order_id = order.order_id;
    return f;
}

}  // namespace qf
