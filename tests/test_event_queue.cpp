#include "quantforge/event_queue.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "quantforge/event.hpp"
#include "quantforge/types.hpp"

using namespace qf;

namespace {

MarketEvent bar(Timestamp ts, Price close) {
    MarketEvent m;
    m.ts = ts;
    m.symbol = 0;
    m.kind = MarketKind::Bar;
    m.close = close;
    return m;
}

SignalEvent signal(Timestamp ts) {
    SignalEvent s;
    s.ts = ts;
    s.symbol = 0;
    s.direction = SignalDirection::Long;
    return s;
}

OrderEvent order(Timestamp ts) {
    OrderEvent o;
    o.ts = ts;
    o.symbol = 0;
    o.side = Side::Buy;
    o.quantity = 1.0;
    return o;
}

}  // namespace

TEST(EventQueue, EmptyByDefault) {
    EventQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.pop().has_value());
}

TEST(EventQueue, PopsInTimestampOrder) {
    EventQueue q;
    q.emit(bar(300, 3.0));
    q.emit(bar(100, 1.0));
    q.emit(bar(200, 2.0));

    std::vector<Timestamp> order_out;
    while (auto ev = q.pop()) {
        order_out.push_back(ev->ts);
    }
    EXPECT_EQ(order_out, (std::vector<Timestamp>{100, 200, 300}));
}

// At a shared timestamp, logical priority must hold: Market before Signal
// before Order. This is the structural no-lookahead ordering.
TEST(EventQueue, PriorityBreaksTimestampTies) {
    EventQueue q;
    // Insert out of logical order on purpose.
    q.emit(order(100));
    q.emit(signal(100));
    q.emit(bar(100, 1.0));

    auto first = q.pop();
    auto second = q.pop();
    auto third = q.pop();
    ASSERT_TRUE(first && second && third);

    EXPECT_EQ(first->priority, EventPriority::Market);
    EXPECT_EQ(second->priority, EventPriority::Signal);
    EXPECT_EQ(third->priority, EventPriority::Order);
}

// Among events of identical (timestamp, priority), FIFO insertion order wins,
// giving a total deterministic ordering.
TEST(EventQueue, SequenceBreaksPriorityTies) {
    EventQueue q;
    q.emit(bar(100, 10.0));
    q.emit(bar(100, 20.0));
    q.emit(bar(100, 30.0));

    std::vector<Price> closes;
    while (auto ev = q.pop()) {
        closes.push_back(std::get<MarketEvent>(ev->payload).close);
    }
    EXPECT_EQ(closes, (std::vector<Price>{10.0, 20.0, 30.0}));
}

TEST(EventQueue, ClearResetsState) {
    EventQueue q;
    q.emit(bar(100, 1.0));
    q.emit(bar(200, 2.0));
    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(EventType, NamesMatchPayload) {
    EXPECT_STREQ(eventTypeName(Event(bar(1, 1.0))), "MarketEvent");
    EXPECT_STREQ(eventTypeName(Event(signal(1))), "SignalEvent");
    EXPECT_STREQ(eventTypeName(Event(order(1))), "OrderEvent");
}
