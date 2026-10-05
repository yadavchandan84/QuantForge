#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "quantforge/analytics.hpp"

using namespace qf;

namespace {

std::vector<EquityPoint> curveFrom(const std::vector<double>& eq) {
    std::vector<EquityPoint> c;
    Timestamp ts = 0;
    for (double e : eq) {
        c.push_back(EquityPoint{ts, e});
        ts += kNanosPerDay;
    }
    return c;
}

}  // namespace

TEST(Analytics, PeriodReturns) {
    auto c = curveFrom({100.0, 110.0, 99.0});
    auto r = periodReturns(c);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_NEAR(r[0], 0.10, 1e-12);  // 110/100 - 1
    EXPECT_NEAR(r[1], 99.0 / 110.0 - 1.0, 1e-12);
}

TEST(Analytics, MeanAndStdev) {
    std::vector<double> xs{0.02, -0.01, 0.03, 0.00, 0.01};
    EXPECT_NEAR(mean(xs), 0.01, 1e-12);
    EXPECT_NEAR(sampleStdev(xs), 0.015811388300841896, 1e-12);
}

TEST(Analytics, SharpeKnownAnswer) {
    std::vector<double> r{0.02, -0.01, 0.03, 0.00, 0.01};
    EXPECT_NEAR(sharpeRatio(r, 252.0), 10.039920318408907, 1e-9);
}

TEST(Analytics, SharpeZeroWhenNoVariance) {
    std::vector<double> r{0.01, 0.01, 0.01};
    EXPECT_DOUBLE_EQ(sharpeRatio(r, 252.0), 0.0);
}

TEST(Analytics, SortinoKnownAnswer) {
    std::vector<double> r{0.02, -0.01, 0.03, 0.00, 0.01};
    EXPECT_NEAR(sortinoRatio(r, 252.0), 35.49647869859769, 1e-9);
}

TEST(Analytics, SortinoZeroWhenNoDownside) {
    std::vector<double> r{0.01, 0.02, 0.03};
    EXPECT_DOUBLE_EQ(sortinoRatio(r, 252.0), 0.0);
}

TEST(Analytics, AnnualizedVolatility) {
    std::vector<double> r{0.02, -0.01, 0.03, 0.00, 0.01};
    EXPECT_NEAR(annualizedVolatility(r, 252.0), 0.25099800796022265, 1e-12);
}

TEST(Analytics, MaxDrawdownKnownAnswer) {
    auto c = curveFrom({100.0, 120.0, 90.0, 110.0, 80.0});
    // Peak 120, trough 80 -> dd = (120-80)/120 = 1/3.
    EXPECT_NEAR(maxDrawdown(c), 1.0 / 3.0, 1e-12);
}

TEST(Analytics, MaxDrawdownZeroForMonotonic) {
    auto c = curveFrom({100.0, 101.0, 102.0, 103.0});
    EXPECT_DOUBLE_EQ(maxDrawdown(c), 0.0);
}

TEST(Analytics, ReportHitRateAndTurnover) {
    auto c = curveFrom({100.0, 110.0, 105.0});  // mean equity = 105
    std::vector<TradeRecord> trades{
        {0, 0, +10.0, 1000.0},
        {0, 0, -5.0, 500.0},
        {0, 0, +3.0, 300.0},
        {0, 0, 0.0, 200.0},  // zero PnL: excluded from hit rate, counts turnover
    };
    auto rep = computeReport(c, trades, 252.0);

    EXPECT_EQ(rep.num_trades, 3u);                // zero-PnL excluded
    EXPECT_NEAR(rep.hit_rate, 2.0 / 3.0, 1e-12);  // 2 wins of 3

    // Turnover = total notional / mean equity = 2000 / 105.
    EXPECT_NEAR(rep.turnover, 2000.0 / 105.0, 1e-9);

    // total_return = 105/100 - 1 = 0.05
    EXPECT_NEAR(rep.total_return, 0.05, 1e-12);
}

TEST(Analytics, EquityCurveCsvRoundTrip) {
    auto c = curveFrom({100.0, 101.5, 99.25});
    const std::string path = "_test_equity.csv";
    writeEquityCurveCsv(path, c);

    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    std::string header;
    std::getline(in, header);
    EXPECT_EQ(header, "timestamp,equity");

    std::vector<double> equities;
    std::string line;
    while (std::getline(in, line)) {
        auto comma = line.find(',');
        ASSERT_NE(comma, std::string::npos);
        equities.push_back(std::stod(line.substr(comma + 1)));
    }
    in.close();
    std::remove(path.c_str());

    ASSERT_EQ(equities.size(), 3u);
    EXPECT_NEAR(equities[0], 100.0, 1e-6);
    EXPECT_NEAR(equities[1], 101.5, 1e-6);
    EXPECT_NEAR(equities[2], 99.25, 1e-6);
}
