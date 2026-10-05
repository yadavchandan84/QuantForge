#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "quantforge/types.hpp"

namespace qf {

/// One point on the account equity curve.
struct EquityPoint {
    Timestamp ts{};
    double equity{0.0};
};

/// A completed round-trip trade, used for hit-rate and trade statistics. The
/// engine records one of these each time a position is reduced/closed, with the
/// realized PnL of the closed portion.
struct TradeRecord {
    Timestamp ts{};
    SymbolId symbol{kInvalidSymbol};
    double realized_pnl{0.0};
    double notional{0.0};  ///< |price * qty| transacted (for turnover).
};

/// Common calendar conventions for annualizing Sharpe/Sortino.
inline constexpr double kTradingDaysPerYear = 252.0;
inline constexpr double kMinutesPerYear = kTradingDaysPerYear * 390.0;  // US cash session

/// A bundle of performance statistics for a backtest run.
struct PerformanceReport {
    double total_return{0.0};       ///< (end/start - 1) on equity.
    double sharpe{0.0};             ///< annualized.
    double sortino{0.0};            ///< annualized, downside deviation.
    double max_drawdown{0.0};       ///< as a positive fraction (0.2 = -20%).
    double annualized_volatility{0.0};
    double turnover{0.0};           ///< total traded notional / mean equity.
    double hit_rate{0.0};           ///< fraction of winning closed trades.
    std::size_t num_trades{0};
    std::size_t num_periods{0};
};

// ---- Individual metrics (pure functions) --------------------------------

/// Simple period-over-period returns from an equity curve. Returns N-1 values
/// for N points; zero or negative prior equity yields a 0 return for that step.
std::vector<double> periodReturns(const std::vector<EquityPoint>& curve);

/// Arithmetic mean of a sample (0 for empty).
double mean(const std::vector<double>& xs);

/// Sample standard deviation (N-1). 0 for fewer than 2 points.
double sampleStdev(const std::vector<double>& xs);

/// Annualized Sharpe ratio from periodic returns.
/// sharpe = (mean(r) - rf_per_period) / stdev(r) * sqrt(periods_per_year).
/// Returns 0 when stdev is 0.
double sharpeRatio(const std::vector<double>& returns, double periods_per_year,
                   double risk_free_per_period = 0.0);

/// Annualized Sortino ratio: like Sharpe but the denominator is the downside
/// deviation (RMS of returns below the target). Returns 0 when no downside.
double sortinoRatio(const std::vector<double>& returns, double periods_per_year,
                    double target_per_period = 0.0);

/// Maximum drawdown of an equity curve as a positive fraction of the running
/// peak (e.g. 0.25 means the equity fell 25% below its prior high).
double maxDrawdown(const std::vector<EquityPoint>& curve);

/// Annualized volatility of periodic returns.
double annualizedVolatility(const std::vector<double>& returns,
                            double periods_per_year);

// ---- Aggregate report ----------------------------------------------------

/// Computes a full PerformanceReport from an equity curve and trade log.
/// `periods_per_year` annualizes risk metrics (e.g. 252 for daily bars).
PerformanceReport computeReport(const std::vector<EquityPoint>& curve,
                                const std::vector<TradeRecord>& trades,
                                double periods_per_year);

/// Writes the equity curve to a CSV file with header "timestamp,equity".
/// Throws std::runtime_error on I/O failure.
void writeEquityCurveCsv(const std::string& path,
                         const std::vector<EquityPoint>& curve);

}  // namespace qf
