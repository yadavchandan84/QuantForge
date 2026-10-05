#include "quantforge/analytics.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace qf {

std::vector<double> periodReturns(const std::vector<EquityPoint>& curve) {
    std::vector<double> rets;
    if (curve.size() < 2) {
        return rets;
    }
    rets.reserve(curve.size() - 1);
    for (std::size_t i = 1; i < curve.size(); ++i) {
        const double prev = curve[i - 1].equity;
        const double cur = curve[i].equity;
        rets.push_back(prev > 0.0 ? (cur / prev - 1.0) : 0.0);
    }
    return rets;
}

double mean(const std::vector<double>& xs) {
    if (xs.empty()) {
        return 0.0;
    }
    const double s = std::accumulate(xs.begin(), xs.end(), 0.0);
    return s / static_cast<double>(xs.size());
}

double sampleStdev(const std::vector<double>& xs) {
    const auto n = static_cast<double>(xs.size());
    if (n < 2.0) {
        return 0.0;
    }
    const double m = mean(xs);
    double acc = 0.0;
    for (double x : xs) {
        const double d = x - m;
        acc += d * d;
    }
    const double var = acc / (n - 1.0);
    return var > 0.0 ? std::sqrt(var) : 0.0;
}

double sharpeRatio(const std::vector<double>& returns, double periods_per_year,
                   double risk_free_per_period) {
    if (returns.size() < 2 || periods_per_year <= 0.0) {
        return 0.0;
    }
    const double sd = sampleStdev(returns);
    if (sd <= 0.0) {
        return 0.0;
    }
    const double excess = mean(returns) - risk_free_per_period;
    return excess / sd * std::sqrt(periods_per_year);
}

double sortinoRatio(const std::vector<double>& returns, double periods_per_year,
                    double target_per_period) {
    if (returns.size() < 2 || periods_per_year <= 0.0) {
        return 0.0;
    }
    // Downside deviation: RMS of shortfalls below target (count all periods in
    // the denominator, the common Sortino convention).
    double acc = 0.0;
    for (double r : returns) {
        const double shortfall = std::min(0.0, r - target_per_period);
        acc += shortfall * shortfall;
    }
    const double downside = std::sqrt(acc / static_cast<double>(returns.size()));
    if (downside <= 0.0) {
        return 0.0;
    }
    const double excess = mean(returns) - target_per_period;
    return excess / downside * std::sqrt(periods_per_year);
}

double maxDrawdown(const std::vector<EquityPoint>& curve) {
    double peak = -std::numeric_limits<double>::infinity();
    double max_dd = 0.0;
    for (const auto& p : curve) {
        peak = std::max(peak, p.equity);
        if (peak > 0.0) {
            const double dd = (peak - p.equity) / peak;
            max_dd = std::max(max_dd, dd);
        }
    }
    return max_dd;
}

double annualizedVolatility(const std::vector<double>& returns,
                            double periods_per_year) {
    if (periods_per_year <= 0.0) {
        return 0.0;
    }
    return sampleStdev(returns) * std::sqrt(periods_per_year);
}

PerformanceReport computeReport(const std::vector<EquityPoint>& curve,
                                const std::vector<TradeRecord>& trades,
                                double periods_per_year) {
    PerformanceReport r;
    const auto rets = periodReturns(curve);
    r.num_periods = rets.size();

    if (curve.size() >= 2 && curve.front().equity > 0.0) {
        r.total_return = curve.back().equity / curve.front().equity - 1.0;
    }
    r.sharpe = sharpeRatio(rets, periods_per_year);
    r.sortino = sortinoRatio(rets, periods_per_year);
    r.max_drawdown = maxDrawdown(curve);
    r.annualized_volatility = annualizedVolatility(rets, periods_per_year);

    // Turnover: total traded notional divided by mean equity over the curve.
    double traded_notional = 0.0;
    for (const auto& t : trades) {
        traded_notional += std::abs(t.notional);
    }
    double sum_eq = 0.0;
    for (const auto& p : curve) {
        sum_eq += p.equity;
    }
    const double mean_eq =
        curve.empty() ? 0.0 : sum_eq / static_cast<double>(curve.size());
    r.turnover = mean_eq > 0.0 ? traded_notional / mean_eq : 0.0;

    // Hit rate over closed trades (those with a realized PnL component).
    std::size_t wins = 0;
    std::size_t counted = 0;
    for (const auto& t : trades) {
        if (t.realized_pnl != 0.0) {
            ++counted;
            if (t.realized_pnl > 0.0) {
                ++wins;
            }
        }
    }
    r.num_trades = counted;
    r.hit_rate = counted > 0 ? static_cast<double>(wins) /
                                   static_cast<double>(counted)
                             : 0.0;
    return r;
}

void writeEquityCurveCsv(const std::string& path,
                         const std::vector<EquityPoint>& curve) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open '" + path + "' for writing");
    }
    out << "timestamp,equity\n";
    out.setf(std::ios::fixed);
    out.precision(10);
    for (const auto& p : curve) {
        out << p.ts << ',' << p.equity << '\n';
    }
    if (!out) {
        throw std::runtime_error("error writing equity curve to '" + path + "'");
    }
}

}  // namespace qf
