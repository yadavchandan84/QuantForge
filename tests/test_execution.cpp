#include "quantforge/execution_handler.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <random>

#include "quantforge/event.hpp"
#include "quantforge/execution_models.hpp"
#include "quantforge/types.hpp"

using namespace qf;

namespace {

OrderEvent marketOrder(Side side, Quantity qty, Timestamp ts = 1000) {
    OrderEvent o;
    o.ts = ts;
    o.symbol = 0;
    o.side = side;
    o.type = OrderType::Market;
    o.quantity = qty;
    o.order_id = 42;
    return o;
}

MarketSnapshot snap(Price ref, Quantity vol) {
    MarketSnapshot s;
    s.reference = ref;
    s.volume = vol;
    s.bid = ref;
    s.ask = ref;
    return s;
}

ExecutionHandler makeHandler(std::unique_ptr<LatencyModel> lat,
                             std::unique_ptr<SlippageModel> slip,
                             std::unique_ptr<FeeModel> fee,
                             ExecutionConfig cfg = {}, std::uint64_t seed = 0) {
    return ExecutionHandler(std::move(lat), std::move(slip), std::move(fee), cfg,
                            seed);
}

}  // namespace

// ---- Slippage models (known-answer) -------------------------------------
TEST(Slippage, FixedBpsAgainstTaker) {
    FixedBpsSlippage s(10.0);  // 10 bps
    std::mt19937_64 rng(0);
    EXPECT_DOUBLE_EQ(s.apply(100.0, Side::Buy, 1, 0, rng), 100.0 * 1.001);
    EXPECT_DOUBLE_EQ(s.apply(100.0, Side::Sell, 1, 0, rng), 100.0 * 0.999);
}

TEST(Slippage, VolumeImpactScalesWithParticipation) {
    VolumeSlippage s(20.0);  // 20 bps coefficient
    std::mt19937_64 rng(0);
    // Participation = 100/10000 = 0.01 -> sqrt = 0.1 -> impact 2 bps.
    const Price buy = s.apply(100.0, Side::Buy, 100.0, 10000.0, rng);
    EXPECT_NEAR(buy, 100.0 * (1.0 + 2.0 * 1e-4), 1e-9);
    // Full participation -> sqrt(1) -> full 20 bps.
    const Price full = s.apply(100.0, Side::Buy, 10000.0, 10000.0, rng);
    EXPECT_NEAR(full, 100.0 * (1.0 + 20.0 * 1e-4), 1e-9);
}

TEST(Slippage, RandomBpsIsDeterministicForSeed) {
    RandomBpsSlippage s(5.0, 25.0);
    std::mt19937_64 a(77), b(77);
    for (int i = 0; i < 10; ++i) {
        const Price pa = s.apply(100.0, Side::Buy, 1, 0, a);
        const Price pb = s.apply(100.0, Side::Buy, 1, 0, b);
        EXPECT_DOUBLE_EQ(pa, pb);
        // Buy slippage is always adverse (>= reference).
        EXPECT_GE(pa, 100.0);
    }
}

// ---- Fee models (known-answer) ------------------------------------------
TEST(Fees, PerShareAndBps) {
    PerShareFee ps(0.01);
    EXPECT_DOUBLE_EQ(ps.commission(100.0, 250.0), 2.5);

    BpsFee bp(5.0);  // 5 bps of notional
    EXPECT_DOUBLE_EQ(bp.commission(100.0, 100.0), 100.0 * 100.0 * 5.0 * 1e-4);
}

// ---- Latency ------------------------------------------------------------
TEST(Latency, FixedAddsToTimestamp) {
    auto h = makeHandler(std::make_unique<FixedLatency>(500),
                         std::make_unique<FixedBpsSlippage>(0.0),
                         std::make_unique<PerShareFee>(0.0));
    auto f = h.execute(marketOrder(Side::Buy, 10, /*ts=*/1000), snap(100.0, 0.0));
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->ts, 1500);
}

TEST(Latency, RandomIsDeterministicForSeed) {
    std::mt19937_64 a(123), b(123);
    RandomLatency lat(100, 1000);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(lat.sample(a), lat.sample(b));
    }
}

// ---- Full execution: fill composition -----------------------------------
TEST(Execution, FullFillAppliesSlippageAndFees) {
    ExecutionConfig cfg;
    cfg.fill_on_zero_volume = true;
    auto h = makeHandler(std::make_unique<FixedLatency>(0),
                         std::make_unique<FixedBpsSlippage>(10.0),
                         std::make_unique<BpsFee>(5.0), cfg);
    auto f = h.execute(marketOrder(Side::Buy, 100.0), snap(100.0, 0.0));
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->fill_price, 100.1);          // 10 bps slippage on buy
    EXPECT_DOUBLE_EQ(f->quantity, 100.0);
    EXPECT_DOUBLE_EQ(f->remaining, 0.0);
    EXPECT_DOUBLE_EQ(f->commission, 100.1 * 100.0 * 5.0 * 1e-4);  // on fill price
}

TEST(Execution, PartialFillAgainstVolume) {
    ExecutionConfig cfg;
    cfg.max_participation = 0.1;  // 10% of volume
    auto h = makeHandler(std::make_unique<FixedLatency>(0),
                         std::make_unique<FixedBpsSlippage>(0.0),
                         std::make_unique<PerShareFee>(0.0), cfg);
    // Order 1000 vs volume 5000 -> max 500 filled, 500 remaining.
    auto f = h.execute(marketOrder(Side::Buy, 1000.0), snap(50.0, 5000.0));
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->quantity, 500.0);
    EXPECT_DOUBLE_EQ(f->remaining, 500.0);
}

TEST(Execution, NoFillOnZeroVolumeWhenDisallowed) {
    ExecutionConfig cfg;
    cfg.fill_on_zero_volume = false;
    auto h = makeHandler(std::make_unique<FixedLatency>(0),
                         std::make_unique<FixedBpsSlippage>(0.0),
                         std::make_unique<PerShareFee>(0.0), cfg);
    auto f = h.execute(marketOrder(Side::Buy, 10.0), snap(100.0, 0.0));
    EXPECT_FALSE(f.has_value());
}

TEST(Execution, LimitOrderOnlyFillsWhenMarketable) {
    auto h = makeHandler(std::make_unique<FixedLatency>(0),
                         std::make_unique<FixedBpsSlippage>(0.0),
                         std::make_unique<PerShareFee>(0.0));
    OrderEvent o;
    o.ts = 0;
    o.symbol = 0;
    o.side = Side::Buy;
    o.type = OrderType::Limit;
    o.quantity = 10.0;
    o.limit_price = 99.0;

    // Market at 100 > limit 99 -> buy limit does not fill.
    EXPECT_FALSE(h.execute(o, snap(100.0, 0.0)).has_value());
    // Market at 98 <= limit 99 -> fills at min(98, 99) = 98.
    auto f = h.execute(o, snap(98.0, 0.0));
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->fill_price, 98.0);
}

TEST(Execution, CloneIsIndependentAndDeterministic) {
    auto h = makeHandler(std::make_unique<RandomLatency>(100, 1000),
                         std::make_unique<FixedBpsSlippage>(0.0),
                         std::make_unique<PerShareFee>(0.0), {}, /*seed=*/7);
    auto a = h.clone(99);
    auto b = h.clone(99);
    auto fa = a.execute(marketOrder(Side::Buy, 1.0, 0), snap(100.0, 0.0));
    auto fb = b.execute(marketOrder(Side::Buy, 1.0, 0), snap(100.0, 0.0));
    ASSERT_TRUE(fa && fb);
    EXPECT_EQ(fa->ts, fb->ts);  // same seed -> same latency draw
}

TEST(Execution, ReseedReproducesSequence) {
    auto h = makeHandler(std::make_unique<RandomLatency>(100, 1000),
                         std::make_unique<FixedBpsSlippage>(0.0),
                         std::make_unique<PerShareFee>(0.0), {}, /*seed=*/5);
    auto f1 = h.execute(marketOrder(Side::Buy, 1.0, 0), snap(100.0, 0.0));
    h.reseed(5);
    auto f2 = h.execute(marketOrder(Side::Buy, 1.0, 0), snap(100.0, 0.0));
    ASSERT_TRUE(f1 && f2);
    EXPECT_EQ(f1->ts, f2->ts);
}
