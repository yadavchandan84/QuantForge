#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "quantforge/data_handler.hpp"
#include "quantforge/symbol_table.hpp"
#include "quantforge/types.hpp"

using namespace qf;

namespace {

MarketEvent bar(Timestamp ts, Price close) {
    MarketEvent m;
    m.ts = ts;
    m.symbol = 0;
    m.kind = MarketKind::Bar;
    m.open = close;
    m.high = close;
    m.low = close;
    m.close = close;
    m.volume = 1.0;
    return m;
}

// Writes a temporary CSV and returns its path.
std::string writeTempCsv(const std::string& name, const std::string& contents) {
    const std::string path = std::string(name);
    std::ofstream out(path);
    out << contents;
    out.close();
    return path;
}

}  // namespace

TEST(DataHandler, StreamsInOrderAndFinishes) {
    std::vector<MarketEvent> evs{bar(100, 1.0), bar(200, 2.0), bar(300, 3.0)};
    CsvBarDataHandler dh(evs);

    EXPECT_EQ(dh.size(), 3u);
    EXPECT_FALSE(dh.finished());

    std::vector<Timestamp> seen;
    while (auto m = dh.next()) {
        seen.push_back(m->ts);
    }
    EXPECT_TRUE(dh.finished());
    EXPECT_EQ(seen, (std::vector<Timestamp>{100, 200, 300}));
}

// No-lookahead is structural: the only accessor is next(), which returns the
// current event and advances. At any point, a consumer that has pulled k events
// has seen exactly events [0, k) and nothing beyond. We assert that the k-th
// pull never reveals a timestamp greater than it should, and that there is no
// way to observe event k+1 before event k.
TEST(DataHandler, NoLookaheadByConstruction) {
    std::vector<MarketEvent> evs{bar(10, 1.0), bar(20, 2.0), bar(30, 3.0), bar(40, 4.0)};
    CsvBarDataHandler dh(evs);

    Timestamp prev = std::numeric_limits<Timestamp>::min();
    std::size_t pulled = 0;
    while (auto m = dh.next()) {
        // Each revealed event is the immediate chronological successor; it is
        // never possible to receive event i before event i-1.
        EXPECT_GE(m->ts, prev);
        EXPECT_EQ(m->close, static_cast<Price>(pulled + 1));
        prev = m->ts;
        ++pulled;
    }
    EXPECT_EQ(pulled, evs.size());
}

TEST(DataHandler, RejectsOutOfOrderTimestamps) {
    std::vector<MarketEvent> evs{bar(100, 1.0), bar(90, 2.0)};
    EXPECT_THROW({ CsvBarDataHandler dh(evs); }, std::runtime_error);
}

TEST(DataHandler, ResetReplaysSameSequence) {
    std::vector<MarketEvent> evs{bar(1, 1.0), bar(2, 2.0)};
    CsvBarDataHandler dh(evs);

    std::vector<Price> run1;
    while (auto m = dh.next())
        run1.push_back(m->close);

    dh.reset();
    EXPECT_FALSE(dh.finished());

    std::vector<Price> run2;
    while (auto m = dh.next())
        run2.push_back(m->close);

    EXPECT_EQ(run1, run2);
}

TEST(DataHandler, LoadsCsvFile) {
    const std::string csv =
        "date,open,high,low,close,volume\n"
        "2023-01-02,100.0,101.5,99.2,101.0,1200000\n"
        "2023-01-03,101.0,102.3,100.1,100.5,1350000\n";
    const std::string path = writeTempCsv("_test_bars.csv", csv);

    SymbolTable symbols;
    CsvBarOptions opts;  // defaults match this layout (DateYMD)
    CsvBarDataHandler dh(path, "TEST", symbols, opts);

    ASSERT_EQ(dh.size(), 2u);
    auto first = dh.next();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->symbol, symbols.lookup("TEST"));
    EXPECT_DOUBLE_EQ(first->close, 101.0);
    EXPECT_DOUBLE_EQ(first->high, 101.5);

    auto second = dh.next();
    ASSERT_TRUE(second.has_value());
    EXPECT_GT(second->ts, first->ts);
    EXPECT_DOUBLE_EQ(second->close, 100.5);

    std::remove(path.c_str());
}

TEST(DataHandler, ParsesTimestampFormats) {
    using TF = CsvBarOptions::TimeFormat;
    // 2023-01-02 is 19359 days after epoch -> *86400*1e9 ns.
    const Timestamp day = parseTimestamp("2023-01-02", TF::DateYMD);
    EXPECT_EQ(day % kNanosPerDay, 0);
    EXPECT_EQ(day / kNanosPerDay, 19359);

    EXPECT_EQ(parseTimestamp("1000", TF::EpochSeconds), 1000 * kNanosPerSecond);
    EXPECT_EQ(parseTimestamp("1000", TF::EpochMillis), 1000 * kNanosPerMilli);
    EXPECT_EQ(parseTimestamp("1000", TF::EpochNanos), 1000);

    const Timestamp dt = parseTimestamp("2023-01-02 09:30:00", TF::DateTimeYMDHMS);
    EXPECT_EQ(dt, day + 9 * 3600 * kNanosPerSecond + 30 * 60 * kNanosPerSecond);
}
