#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace qf {

/// Nanoseconds since the Unix epoch. Using a signed 64-bit integer gives a
/// range of roughly +/- 292 years around 1970, which is ample and keeps all
/// time arithmetic in exact integer units (no floating point drift).
using Timestamp = std::int64_t;

/// Monotonic sequence number used to break ties between events that share the
/// same timestamp, guaranteeing a total, deterministic ordering.
using SeqNum = std::uint64_t;

/// Price and quantity are represented as double. For a research backtester this
/// is a deliberate tradeoff: doubles are fast and interoperate cleanly with
/// NumPy/pandas on the Python side. A production matching engine would use
/// fixed-point integer ticks instead (see MatchCore).
using Price = double;
using Quantity = double;

/// Instrument identifier. Interned to a small integer for fast comparison and
/// compact storage in the hot event path.
using SymbolId = std::uint32_t;

inline constexpr SymbolId kInvalidSymbol = static_cast<SymbolId>(-1);

/// Side of an order or position.
enum class Side : std::uint8_t {
    Buy = 0,
    Sell = 1,
};

constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

/// Sign multiplier for a side: +1 for Buy, -1 for Sell.
constexpr int sideSign(Side s) noexcept {
    return s == Side::Buy ? 1 : -1;
}

constexpr std::string_view toString(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

/// Order type supported by the execution handler.
enum class OrderType : std::uint8_t {
    Market = 0,
    Limit = 1,
};

constexpr std::string_view toString(OrderType t) noexcept {
    switch (t) {
        case OrderType::Market:
            return "MARKET";
        case OrderType::Limit:
            return "LIMIT";
    }
    return "UNKNOWN";
}

/// Direction of a strategy signal.
enum class SignalDirection : std::uint8_t {
    Long = 0,
    Short = 1,
    Exit = 2,  ///< Flatten any existing position.
};

constexpr std::string_view toString(SignalDirection d) noexcept {
    switch (d) {
        case SignalDirection::Long:
            return "LONG";
        case SignalDirection::Short:
            return "SHORT";
        case SignalDirection::Exit:
            return "EXIT";
    }
    return "UNKNOWN";
}

/// Convenience: nanoseconds in common units, for building timestamps in tests
/// and data loaders without magic numbers scattered around.
inline constexpr Timestamp kNanosPerMicro = 1'000;
inline constexpr Timestamp kNanosPerMilli = 1'000'000;
inline constexpr Timestamp kNanosPerSecond = 1'000'000'000;
inline constexpr Timestamp kNanosPerMinute = 60 * kNanosPerSecond;
inline constexpr Timestamp kNanosPerDay = 24 * 60 * kNanosPerMinute;

}  // namespace qf
