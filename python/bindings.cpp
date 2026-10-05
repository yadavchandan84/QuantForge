// pybind11 bindings for QuantForge.
//
// Exposes a NumPy/pandas-friendly API. The design goal is that a researcher can
// pass bar arrays straight from a DataFrame, pick a strategy and cost model with
// plain Python values, and get back results as NumPy arrays ready to plot.
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "quantforge/analytics.hpp"
#include "quantforge/data_handler.hpp"
#include "quantforge/engine.hpp"
#include "quantforge/execution_handler.hpp"
#include "quantforge/execution_models.hpp"
#include "quantforge/strategies.hpp"
#include "quantforge/sweep.hpp"
#include "quantforge/types.hpp"

namespace py = pybind11;
using namespace qf;

namespace {

// ---- Helpers to assemble components from plain-Python specs ---------------

std::vector<MarketEvent> barsFromArrays(const py::array_t<std::int64_t>& ts,
                                        const py::array_t<double>& open,
                                        const py::array_t<double>& high,
                                        const py::array_t<double>& low,
                                        const py::array_t<double>& close,
                                        const py::array_t<double>& volume) {
    auto n = static_cast<std::size_t>(ts.size());
    if (static_cast<std::size_t>(open.size()) != n ||
        static_cast<std::size_t>(high.size()) != n ||
        static_cast<std::size_t>(low.size()) != n ||
        static_cast<std::size_t>(close.size()) != n ||
        static_cast<std::size_t>(volume.size()) != n) {
        throw std::invalid_argument("all bar arrays must have the same length");
    }
    auto t = ts.unchecked<1>();
    auto o = open.unchecked<1>();
    auto h = high.unchecked<1>();
    auto l = low.unchecked<1>();
    auto c = close.unchecked<1>();
    auto v = volume.unchecked<1>();

    std::vector<MarketEvent> bars;
    bars.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        MarketEvent m;
        m.ts = t(static_cast<py::ssize_t>(i));
        m.symbol = 0;
        m.kind = MarketKind::Bar;
        m.open = o(static_cast<py::ssize_t>(i));
        m.high = h(static_cast<py::ssize_t>(i));
        m.low = l(static_cast<py::ssize_t>(i));
        m.close = c(static_cast<py::ssize_t>(i));
        m.volume = v(static_cast<py::ssize_t>(i));
        bars.push_back(m);
    }
    return bars;
}

std::unique_ptr<Strategy> makeStrategy(const std::string& name,
                                       const py::dict& params) {
    auto getU = [&](const char* key, std::size_t dflt) -> std::size_t {
        return params.contains(key) ? params[key].cast<std::size_t>() : dflt;
    };
    auto getD = [&](const char* key, double dflt) -> double {
        return params.contains(key) ? params[key].cast<double>() : dflt;
    };

    if (name == "ma_crossover") {
        return std::make_unique<MovingAverageCrossover>(0, getU("fast", 10),
                                                        getU("slow", 30));
    }
    if (name == "mean_reversion") {
        return std::make_unique<MeanReversion>(0, getU("lookback", 20),
                                               getD("entry_z", 1.5),
                                               getD("exit_z", 0.5));
    }
    if (name == "market_maker") {
        return std::make_unique<MarketMaker>(0, getU("fair_lookback", 20),
                                             getD("band", 0.01),
                                             getD("max_inventory", 5.0));
    }
    throw std::invalid_argument("unknown strategy '" + name + "'");
}

std::unique_ptr<LatencyModel> makeLatency(const py::dict& spec) {
    const std::string kind =
        spec.contains("kind") ? spec["kind"].cast<std::string>() : "fixed";
    if (kind == "fixed") {
        const Timestamp ns =
            spec.contains("ns") ? spec["ns"].cast<Timestamp>() : 0;
        return std::make_unique<FixedLatency>(ns);
    }
    if (kind == "random") {
        const Timestamp lo =
            spec.contains("min_ns") ? spec["min_ns"].cast<Timestamp>() : 0;
        const Timestamp hi =
            spec.contains("max_ns") ? spec["max_ns"].cast<Timestamp>() : 0;
        return std::make_unique<RandomLatency>(lo, hi);
    }
    throw std::invalid_argument("unknown latency kind '" + kind + "'");
}

std::unique_ptr<SlippageModel> makeSlippage(const py::dict& spec) {
    const std::string kind =
        spec.contains("kind") ? spec["kind"].cast<std::string>() : "fixed_bps";
    if (kind == "fixed_bps") {
        return std::make_unique<FixedBpsSlippage>(
            spec.contains("bps") ? spec["bps"].cast<double>() : 0.0);
    }
    if (kind == "volume") {
        return std::make_unique<VolumeSlippage>(
            spec.contains("coeff_bps") ? spec["coeff_bps"].cast<double>() : 0.0);
    }
    if (kind == "random_bps") {
        return std::make_unique<RandomBpsSlippage>(
            spec.contains("min_bps") ? spec["min_bps"].cast<double>() : 0.0,
            spec.contains("max_bps") ? spec["max_bps"].cast<double>() : 0.0);
    }
    throw std::invalid_argument("unknown slippage kind '" + kind + "'");
}

std::unique_ptr<FeeModel> makeFee(const py::dict& spec) {
    const std::string kind =
        spec.contains("kind") ? spec["kind"].cast<std::string>() : "per_share";
    if (kind == "per_share") {
        return std::make_unique<PerShareFee>(
            spec.contains("fee") ? spec["fee"].cast<double>() : 0.0);
    }
    if (kind == "bps") {
        return std::make_unique<BpsFee>(
            spec.contains("bps") ? spec["bps"].cast<double>() : 0.0);
    }
    throw std::invalid_argument("unknown fee kind '" + kind + "'");
}

ExecutionHandler makeExecution(const py::dict& latency, const py::dict& slippage,
                               const py::dict& fee, const ExecutionConfig& cfg,
                               std::uint64_t seed) {
    return ExecutionHandler(makeLatency(latency), makeSlippage(slippage),
                            makeFee(fee), cfg, seed);
}

// Converts a BacktestResult into a Python dict with NumPy arrays.
py::dict resultToDict(const BacktestResult& r) {
    py::dict out;

    // Equity curve as two aligned NumPy arrays.
    const auto n = static_cast<py::ssize_t>(r.equity_curve.size());
    py::array_t<std::int64_t> ts(n);
    py::array_t<double> eq(n);
    auto tsm = ts.mutable_unchecked<1>();
    auto eqm = eq.mutable_unchecked<1>();
    for (py::ssize_t i = 0; i < n; ++i) {
        tsm(i) = r.equity_curve[static_cast<std::size_t>(i)].ts;
        eqm(i) = r.equity_curve[static_cast<std::size_t>(i)].equity;
    }
    out["equity_ts"] = ts;
    out["equity"] = eq;

    // Trade realized PnL + notional as arrays.
    const auto m = static_cast<py::ssize_t>(r.trades.size());
    py::array_t<double> pnl(m);
    py::array_t<double> notional(m);
    auto pm = pnl.mutable_unchecked<1>();
    auto nm = notional.mutable_unchecked<1>();
    for (py::ssize_t i = 0; i < m; ++i) {
        pm(i) = r.trades[static_cast<std::size_t>(i)].realized_pnl;
        nm(i) = r.trades[static_cast<std::size_t>(i)].notional;
    }
    out["trade_pnl"] = pnl;
    out["trade_notional"] = notional;

    // Scalar summary.
    py::dict report;
    report["total_return"] = r.report.total_return;
    report["sharpe"] = r.report.sharpe;
    report["sortino"] = r.report.sortino;
    report["max_drawdown"] = r.report.max_drawdown;
    report["annualized_volatility"] = r.report.annualized_volatility;
    report["turnover"] = r.report.turnover;
    report["hit_rate"] = r.report.hit_rate;
    report["num_trades"] = r.report.num_trades;
    report["num_periods"] = r.report.num_periods;
    out["report"] = report;

    out["final_equity"] = r.final_equity;
    out["final_cash"] = r.final_cash;
    out["total_commission"] = r.total_commission;
    out["num_fills"] = r.num_fills;
    out["num_signals"] = r.num_signals;
    out["num_orders"] = r.num_orders;
    out["num_market_events"] = r.num_market_events;
    return out;
}

ExecutionConfig execConfigFrom(const py::dict& d) {
    ExecutionConfig cfg;
    if (d.contains("max_participation")) {
        cfg.max_participation = d["max_participation"].cast<double>();
    }
    if (d.contains("fill_on_zero_volume")) {
        cfg.fill_on_zero_volume = d["fill_on_zero_volume"].cast<bool>();
    }
    return cfg;
}

RiskLimits riskFrom(const py::dict& d) {
    RiskLimits r;
    if (d.contains("max_position")) {
        r.max_position = d["max_position"].cast<double>();
    }
    if (d.contains("max_gross_exposure")) {
        r.max_gross_exposure = d["max_gross_exposure"].cast<double>();
    }
    return r;
}

// ---- High-level entry point ----------------------------------------------

py::dict runBacktest(py::array_t<std::int64_t> ts, py::array_t<double> open,
                     py::array_t<double> high, py::array_t<double> low,
                     py::array_t<double> close, py::array_t<double> volume,
                     const std::string& strategy, py::dict strategy_params,
                     py::dict latency, py::dict slippage, py::dict fee,
                     py::dict execution_cfg, py::dict risk, double initial_cash,
                     double target_position, double periods_per_year,
                     std::uint64_t seed) {
    auto bars = barsFromArrays(ts, open, high, low, close, volume);
    CsvBarDataHandler data(std::move(bars));
    auto strat = makeStrategy(strategy, strategy_params);

    BacktestConfig cfg;
    cfg.initial_cash = initial_cash;
    cfg.target_position = target_position;
    cfg.periods_per_year = periods_per_year;
    cfg.seed = seed;
    cfg.risk = riskFrom(risk);
    cfg.execution = execConfigFrom(execution_cfg);

    auto exec = makeExecution(latency, slippage, fee, cfg.execution, seed);
    Engine engine(data, *strat, std::move(exec), cfg);
    const auto result = engine.run();
    return resultToDict(result);
}

}  // namespace

PYBIND11_MODULE(_core, m) {
    m.doc() = "QuantForge: event-driven backtesting & trading simulator (C++ core)";
    m.attr("__version__") = "0.1.0";

    py::enum_<Side>(m, "Side")
        .value("Buy", Side::Buy)
        .value("Sell", Side::Sell);
    py::enum_<SignalDirection>(m, "SignalDirection")
        .value("Long", SignalDirection::Long)
        .value("Short", SignalDirection::Short)
        .value("Exit", SignalDirection::Exit);

    m.def("run_backtest", &runBacktest, py::arg("ts"), py::arg("open"),
          py::arg("high"), py::arg("low"), py::arg("close"), py::arg("volume"),
          py::arg("strategy"), py::arg("strategy_params") = py::dict(),
          py::arg("latency") = py::dict(), py::arg("slippage") = py::dict(),
          py::arg("fee") = py::dict(), py::arg("execution_cfg") = py::dict(),
          py::arg("risk") = py::dict(), py::arg("initial_cash") = 100000.0,
          py::arg("target_position") = 100.0,
          py::arg("periods_per_year") = kTradingDaysPerYear,
          py::arg("seed") = 0,
          R"doc(Run a single backtest over OHLCV bar arrays.

Returns a dict with NumPy arrays 'equity_ts', 'equity', 'trade_pnl',
'trade_notional', a nested 'report' dict of performance metrics, and scalar
summaries (final_equity, num_fills, ...).
)doc");

    // Standalone analytics on an existing equity curve (equity values array).
    m.def(
        "sharpe",
        [](py::array_t<double> equity, double ppy) {
            auto e = equity.unchecked<1>();
            std::vector<EquityPoint> curve;
            curve.reserve(static_cast<std::size_t>(equity.size()));
            for (py::ssize_t i = 0; i < equity.size(); ++i) {
                curve.push_back(EquityPoint{i, e(i)});
            }
            return sharpeRatio(periodReturns(curve), ppy);
        },
        py::arg("equity"), py::arg("periods_per_year") = kTradingDaysPerYear,
        "Annualized Sharpe ratio of an equity-value array.");

    m.def(
        "max_drawdown",
        [](py::array_t<double> equity) {
            auto e = equity.unchecked<1>();
            std::vector<EquityPoint> curve;
            curve.reserve(static_cast<std::size_t>(equity.size()));
            for (py::ssize_t i = 0; i < equity.size(); ++i) {
                curve.push_back(EquityPoint{i, e(i)});
            }
            return maxDrawdown(curve);
        },
        py::arg("equity"), "Maximum drawdown (positive fraction) of an equity array.");
}
