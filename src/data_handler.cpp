#include "quantforge/data_handler.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace qf {
namespace {

// Days from civil date (Howard Hinnant's algorithm), proleptic Gregorian,
// returns days since 1970-01-01. Avoids timegm/_mkgmtime portability issues.
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = m > 2 ? m - 3 : m + 9;
    const unsigned doy = (153 * mp + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

double parseDouble(const std::string& s) {
    // from_chars for double is not available on all stdlibs for floats in a
    // uniform way across compilers; strtod is reliable and locale-C by default
    // here. Validate that it consumed non-empty input.
    const char* begin = s.c_str();
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin) {
        throw std::runtime_error("CSV: could not parse number from '" + s + "'");
    }
    return v;
}

std::string trim(std::string_view sv) {
    std::size_t b = 0;
    std::size_t e = sv.size();
    while (b < e && std::isspace(static_cast<unsigned char>(sv[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(sv[e - 1]))) --e;
    return std::string(sv.substr(b, e - b));
}

std::vector<std::string> splitRow(const std::string& line, char delim) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, delim)) {
        out.push_back(trim(cur));
    }
    return out;
}

}  // namespace

Timestamp parseTimestamp(const std::string& field, CsvBarOptions::TimeFormat fmt) {
    using TF = CsvBarOptions::TimeFormat;
    switch (fmt) {
        case TF::EpochSeconds:
            return static_cast<Timestamp>(parseDouble(field)) * kNanosPerSecond;
        case TF::EpochMillis:
            return static_cast<Timestamp>(parseDouble(field)) * kNanosPerMilli;
        case TF::EpochNanos:
            return static_cast<Timestamp>(std::strtoll(field.c_str(), nullptr, 10));
        case TF::DateYMD:
        case TF::DateTimeYMDHMS: {
            // Expect "YYYY-MM-DD" optionally followed by " HH:MM:SS" or
            // "THH:MM:SS".
            int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
            char sep = 0;
            std::istringstream ss(field);
            ss >> y;
            ss.get();  // '-'
            ss >> mo;
            ss.get();  // '-'
            ss >> d;
            if (fmt == TF::DateTimeYMDHMS && ss.peek() != EOF) {
                ss.get(sep);  // ' ' or 'T'
                ss >> h;
                ss.get();  // ':'
                ss >> mi;
                ss.get();  // ':'
                ss >> s;
            }
            if (y == 0 && mo == 0 && d == 0) {
                throw std::runtime_error("CSV: bad date '" + field + "'");
            }
            const std::int64_t days = daysFromCivil(y, static_cast<unsigned>(mo),
                                                    static_cast<unsigned>(d));
            const std::int64_t secs =
                days * 86400 + h * 3600 + mi * 60 + s;
            return static_cast<Timestamp>(secs) * kNanosPerSecond;
        }
    }
    throw std::runtime_error("CSV: unsupported time format");
}

CsvBarDataHandler::CsvBarDataHandler(const std::string& path, const std::string& symbol,
                                     SymbolTable& symbols, const CsvBarOptions& opts) {
    symbol_id_ = symbols.intern(symbol);

    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("CSV: cannot open file '" + path + "'");
    }

    std::string line;
    std::size_t line_no = 0;
    bool first = true;
    while (std::getline(in, line)) {
        ++line_no;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // tolerate CRLF
        }
        if (line.empty()) {
            continue;
        }
        if (first && opts.has_header) {
            first = false;
            continue;
        }
        first = false;

        const auto cols = splitRow(line, opts.delimiter);
        const int max_col = std::max({opts.ts_col, opts.open_col, opts.high_col,
                                      opts.low_col, opts.close_col, opts.volume_col});
        if (static_cast<int>(cols.size()) <= max_col) {
            throw std::runtime_error("CSV: too few columns at line " +
                                     std::to_string(line_no));
        }

        MarketEvent m;
        m.symbol = symbol_id_;
        m.kind = MarketKind::Bar;
        m.ts = parseTimestamp(cols[static_cast<std::size_t>(opts.ts_col)], opts.time_format);
        m.open = parseDouble(cols[static_cast<std::size_t>(opts.open_col)]);
        m.high = parseDouble(cols[static_cast<std::size_t>(opts.high_col)]);
        m.low = parseDouble(cols[static_cast<std::size_t>(opts.low_col)]);
        m.close = parseDouble(cols[static_cast<std::size_t>(opts.close_col)]);
        m.volume = parseDouble(cols[static_cast<std::size_t>(opts.volume_col)]);
        events_.push_back(m);
    }

    validateOrdering();
}

CsvBarDataHandler::CsvBarDataHandler(std::vector<MarketEvent> events)
    : events_(std::move(events)) {
    if (!events_.empty()) {
        symbol_id_ = events_.front().symbol;
    }
    validateOrdering();
}

void CsvBarDataHandler::validateOrdering() const {
    for (std::size_t i = 1; i < events_.size(); ++i) {
        if (events_[i].ts < events_[i - 1].ts) {
            throw std::runtime_error(
                "DataHandler: timestamps not in non-decreasing order at index " +
                std::to_string(i));
        }
    }
}

std::optional<MarketEvent> CsvBarDataHandler::next() {
    if (cursor_ >= events_.size()) {
        return std::nullopt;
    }
    return events_[cursor_++];
}

bool CsvBarDataHandler::finished() const { return cursor_ >= events_.size(); }

std::size_t CsvBarDataHandler::size() const { return events_.size(); }

void CsvBarDataHandler::reset() { cursor_ = 0; }

}  // namespace qf
