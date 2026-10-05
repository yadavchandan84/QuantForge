#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "quantforge/event.hpp"
#include "quantforge/symbol_table.hpp"
#include "quantforge/types.hpp"

namespace qf {

/// Abstract streaming market-data source.
///
/// No-lookahead by design: the only way to obtain data is `next()`, which
/// returns the chronologically next MarketEvent and advances an internal
/// cursor. There is no API to peek at future rows, index arbitrary bars, or
/// rewind, so a strategy structurally cannot see data it would not have had in
/// real time. Events are guaranteed to be emitted in non-decreasing timestamp
/// order; construction validates this and throws on an out-of-order file.
class DataHandler {
  public:
    virtual ~DataHandler() = default;

    /// Returns the next event in time order, or nullopt when the stream is
    /// exhausted. Advances the cursor.
    virtual std::optional<MarketEvent> next() = 0;

    /// True once all data has been consumed.
    virtual bool finished() const = 0;

    /// Total number of events in the stream (for progress/benchmark reporting).
    virtual std::size_t size() const = 0;

    /// Rewinds to the beginning so the same data can drive another run. Used by
    /// parameter sweeps and deterministic-replay tests, never exposed to
    /// strategies.
    virtual void reset() = 0;
};

/// Column mapping / parsing options for the CSV bar loader.
struct CsvBarOptions {
    char delimiter{','};
    bool has_header{true};

    // Column indices (0-based) within each row.
    int ts_col{0};
    int open_col{1};
    int high_col{2};
    int low_col{3};
    int close_col{4};
    int volume_col{5};

    /// How to interpret the timestamp column.
    enum class TimeFormat {
        EpochSeconds,
        EpochMillis,
        EpochNanos,
        DateYMD,       ///< "YYYY-MM-DD" -> midnight UTC.
        DateTimeYMDHMS ///< "YYYY-MM-DD HH:MM:SS" -> UTC.
    } time_format{TimeFormat::DateYMD};
};

/// In-memory, strictly time-ordered bar data loaded from a CSV file.
///
/// Loading reads the whole file once, parses rows into MarketEvents, and
/// verifies non-decreasing timestamps. Streaming then walks the vector via a
/// forward-only cursor.
class CsvBarDataHandler final : public DataHandler {
  public:
    /// Loads bars for `symbol` from `path`. `symbol` is interned in `symbols`.
    /// Throws std::runtime_error on I/O or parse errors, or if timestamps are
    /// not monotonically non-decreasing.
    CsvBarDataHandler(const std::string& path, const std::string& symbol,
                      SymbolTable& symbols, const CsvBarOptions& opts = {});

    /// Builds a handler directly from pre-parsed events (used by tests and the
    /// Python bindings). Validates ordering.
    explicit CsvBarDataHandler(std::vector<MarketEvent> events);

    std::optional<MarketEvent> next() override;
    bool finished() const override;
    std::size_t size() const override;
    void reset() override;

    SymbolId symbol() const noexcept { return symbol_id_; }
    const std::vector<MarketEvent>& events() const noexcept { return events_; }

  private:
    void validateOrdering() const;

    std::vector<MarketEvent> events_;
    std::size_t cursor_{0};
    SymbolId symbol_id_{kInvalidSymbol};
};

/// Parses a timestamp string according to `fmt` into nanoseconds since epoch.
/// Exposed for reuse by other loaders and for unit testing.
Timestamp parseTimestamp(const std::string& field, CsvBarOptions::TimeFormat fmt);

}  // namespace qf
