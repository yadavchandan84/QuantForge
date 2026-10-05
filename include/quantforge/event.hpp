#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "quantforge/types.hpp"

namespace qf {

/// Logical processing priority for events sharing the same timestamp. The
/// order is deliberate and is the backbone of the no-lookahead guarantee: at a
/// single instant, market data must be observed before a strategy can act on
/// it, a signal before an order, and an order before its fill.
enum class EventPriority : std::uint8_t {
    Market = 0,
    Signal = 1,
    Order = 2,
    Fill = 3,
};

/// Shape of a market data event. A single struct covers bars, trade ticks, and
/// top-of-book (L1) quotes; unused fields are left at NaN/zero depending on the
/// kind so strategies can branch on `kind`.
enum class MarketKind : std::uint8_t {
    Bar = 0,    ///< OHLCV bar.
    Tick = 1,   ///< Last trade price/size.
    Quote = 2,  ///< L1 best bid/ask.
};

/// Market data event: a tick, bar, or L1 quote for one instrument.
struct MarketEvent {
    Timestamp ts{};
    SymbolId symbol{kInvalidSymbol};
    MarketKind kind{MarketKind::Bar};

    // Bar fields (kind == Bar)
    Price open{0.0};
    Price high{0.0};
    Price low{0.0};
    Price close{0.0};
    Quantity volume{0.0};

    // Tick fields (kind == Tick)
    Price last{0.0};
    Quantity last_size{0.0};

    // Quote fields (kind == Quote)
    Price bid{0.0};
    Price ask{0.0};
    Quantity bid_size{0.0};
    Quantity ask_size{0.0};

    /// Best single reference price for this event, used by execution/portfolio
    /// marking when a more specific price is not relevant.
    Price referencePrice() const noexcept {
        switch (kind) {
            case MarketKind::Bar:
                return close;
            case MarketKind::Tick:
                return last;
            case MarketKind::Quote:
                return (bid + ask) * 0.5;
        }
        return close;
    }
};

/// A directional view emitted by a strategy. `strength` in [0, 1] can scale
/// position sizing; portfolio/sizing logic decides the actual order quantity.
struct SignalEvent {
    Timestamp ts{};
    SymbolId symbol{kInvalidSymbol};
    SignalDirection direction{SignalDirection::Exit};
    double strength{1.0};
    std::uint64_t strategy_id{0};
};

/// An order request produced by portfolio/sizing from a signal.
struct OrderEvent {
    Timestamp ts{};
    SymbolId symbol{kInvalidSymbol};
    Side side{Side::Buy};
    OrderType type{OrderType::Market};
    Quantity quantity{0.0};
    Price limit_price{0.0};  ///< Used only when type == Limit.
    std::uint64_t order_id{0};
};

/// The result of (partially) executing an order against the market.
struct FillEvent {
    Timestamp ts{};
    SymbolId symbol{kInvalidSymbol};
    Side side{Side::Buy};
    Quantity quantity{0.0};  ///< Filled quantity (may be < order quantity).
    Price fill_price{0.0};   ///< Price after slippage.
    double commission{0.0};  ///< Fees/commission in account currency.
    Quantity remaining{0.0}; ///< Unfilled remainder of the originating order.
    std::uint64_t order_id{0};
};

/// Tagged union of every event the engine can carry.
using EventPayload = std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent>;

/// A queue entry: the payload plus the metadata needed for total ordering.
struct Event {
    EventPayload payload;
    Timestamp ts{};
    EventPriority priority{EventPriority::Market};
    SeqNum seq{0};  ///< Insertion order tiebreak; assigned by the queue.

    Event() = default;

    explicit Event(MarketEvent e)
        : payload(e), ts(e.ts), priority(EventPriority::Market) {}
    explicit Event(SignalEvent e)
        : payload(e), ts(e.ts), priority(EventPriority::Signal) {}
    explicit Event(OrderEvent e)
        : payload(e), ts(e.ts), priority(EventPriority::Order) {}
    explicit Event(FillEvent e)
        : payload(e), ts(e.ts), priority(EventPriority::Fill) {}
};

/// Human-readable name of the active payload alternative. Defined in event.cpp.
const char* eventTypeName(const Event& e) noexcept;

}  // namespace qf
