#include "quantforge/portfolio.hpp"

#include <gtest/gtest.h>

#include "quantforge/event.hpp"
#include "quantforge/types.hpp"

using namespace qf;

namespace {

FillEvent fill(Side side, Quantity qty, Price price, double commission = 0.0,
               SymbolId sym = 0) {
    FillEvent f;
    f.ts = 0;
    f.symbol = sym;
    f.side = side;
    f.quantity = qty;
    f.fill_price = price;
    f.commission = commission;
    return f;
}

}  // namespace

TEST(Portfolio, LongRoundTripRealizesPnl) {
    Portfolio p(10000.0);
    p.onFill(fill(Side::Buy, 100, 50.0));
    EXPECT_DOUBLE_EQ(p.cash(), 5000.0);
    EXPECT_DOUBLE_EQ(p.position(0).quantity, 100.0);
    EXPECT_DOUBLE_EQ(p.position(0).avg_price, 50.0);

    p.onMark(0, 55.0);
    EXPECT_DOUBLE_EQ(p.unrealizedPnl(), 500.0);
    EXPECT_DOUBLE_EQ(p.equity(), 10500.0);
    EXPECT_DOUBLE_EQ(p.totalPnl(), 500.0);

    p.onFill(fill(Side::Sell, 100, 55.0));
    EXPECT_DOUBLE_EQ(p.position(0).quantity, 0.0);
    EXPECT_DOUBLE_EQ(p.realizedPnl(), 500.0);
    EXPECT_DOUBLE_EQ(p.cash(), 10500.0);
    EXPECT_DOUBLE_EQ(p.equity(), 10500.0);
}

TEST(Portfolio, ShortPositionAccounting) {
    Portfolio p(10000.0);
    p.onFill(fill(Side::Sell, 100, 50.0));  // open short from flat
    EXPECT_DOUBLE_EQ(p.cash(), 15000.0);
    EXPECT_DOUBLE_EQ(p.position(0).quantity, -100.0);
    EXPECT_DOUBLE_EQ(p.position(0).avg_price, 50.0);

    p.onMark(0, 45.0);  // price fell -> short is profitable
    EXPECT_DOUBLE_EQ(p.unrealizedPnl(), 500.0);
    EXPECT_DOUBLE_EQ(p.equity(), 10500.0);

    p.onFill(fill(Side::Buy, 100, 45.0));  // cover
    EXPECT_DOUBLE_EQ(p.realizedPnl(), 500.0);
    EXPECT_DOUBLE_EQ(p.position(0).quantity, 0.0);
    EXPECT_DOUBLE_EQ(p.equity(), 10500.0);
}

TEST(Portfolio, AveragesCostOnPyramiding) {
    Portfolio p(100000.0);
    p.onFill(fill(Side::Buy, 100, 10.0));
    p.onFill(fill(Side::Buy, 100, 20.0));
    // avg = (100*10 + 100*20) / 200 = 15
    EXPECT_DOUBLE_EQ(p.position(0).quantity, 200.0);
    EXPECT_DOUBLE_EQ(p.position(0).avg_price, 15.0);

    p.onFill(fill(Side::Sell, 50, 25.0));
    // realized on 50 closed: (25 - 15) * 50 = 500
    EXPECT_DOUBLE_EQ(p.realizedPnl(), 500.0);
    EXPECT_DOUBLE_EQ(p.position(0).quantity, 150.0);
    EXPECT_DOUBLE_EQ(p.position(0).avg_price, 15.0);  // unchanged on reduce
}

TEST(Portfolio, CrossThroughZeroOpensOppositeSide) {
    Portfolio p(100000.0);
    p.onFill(fill(Side::Buy, 100, 10.0));          // long 100 @ 10
    p.onFill(fill(Side::Sell, 150, 12.0));         // sell 150: close 100, open short 50
    // Realized on 100 closed long: (12 - 10) * 100 = 200
    EXPECT_DOUBLE_EQ(p.realizedPnl(), 200.0);
    EXPECT_DOUBLE_EQ(p.position(0).quantity, -50.0);
    EXPECT_DOUBLE_EQ(p.position(0).avg_price, 12.0);  // new short entry
}

TEST(Portfolio, CommissionReducesCashAndTracked) {
    Portfolio p(10000.0);
    p.onFill(fill(Side::Buy, 100, 50.0, /*commission=*/7.5));
    EXPECT_DOUBLE_EQ(p.cash(), 10000.0 - 5000.0 - 7.5);
    EXPECT_DOUBLE_EQ(p.totalCommission(), 7.5);
}

// ---- Risk limits ---------------------------------------------------------
TEST(RiskLimits, PositionLimitClampsOrder) {
    RiskLimits lim;
    lim.max_position = 100.0;
    Portfolio p(100000.0, lim);

    // From flat, request 150 long -> clamp to 100.
    EXPECT_DOUBLE_EQ(p.clampOrderQuantity(0, Side::Buy, 150.0, 10.0), 100.0);

    p.onFill(fill(Side::Buy, 80, 10.0));  // now long 80
    // Room to add long = 100 - 80 = 20.
    EXPECT_DOUBLE_EQ(p.clampOrderQuantity(0, Side::Buy, 50.0, 10.0), 20.0);
    // Selling (reducing/going short) up to 180 allowed before hitting -100:
    // projected = 80 - 150 = -70, |.|=70 <= 100 -> full 150 allowed.
    EXPECT_DOUBLE_EQ(p.clampOrderQuantity(0, Side::Sell, 150.0, 10.0), 150.0);
}

TEST(RiskLimits, ExposureLimitClampsOrder) {
    RiskLimits lim;
    lim.max_gross_exposure = 1000.0;  // $1000 gross
    Portfolio p(100000.0, lim);

    // At price 10, max added exposure = 1000 -> max 100 units.
    EXPECT_DOUBLE_EQ(p.clampOrderQuantity(0, Side::Buy, 200.0, 10.0), 100.0);

    // Build exposure to the cap, then new adds are rejected.
    p.onFill(fill(Side::Buy, 100, 10.0));  // gross now 1000
    p.onMark(0, 10.0);
    EXPECT_DOUBLE_EQ(p.clampOrderQuantity(0, Side::Buy, 10.0, 10.0), 0.0);
    // A reducing sell is still allowed.
    EXPECT_GT(p.clampOrderQuantity(0, Side::Sell, 10.0, 10.0), 0.0);
}

TEST(Portfolio, ResetRestoresInitialState) {
    Portfolio p(10000.0);
    p.onFill(fill(Side::Buy, 100, 50.0));
    p.reset();
    EXPECT_DOUBLE_EQ(p.cash(), 10000.0);
    EXPECT_FALSE(p.hasPosition(0));
    EXPECT_DOUBLE_EQ(p.equity(), 10000.0);
}
