"""QuantForge end-to-end demo.

Runs the three built-in strategies on the bundled sample data, prints a metrics
table, checks deterministic replay, and saves an equity-curve chart.

    python examples/demo.py            # saves results/equity_curves.png and opens it
    python examples/demo.py --no-show  # save only
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import numpy as np
import pandas as pd

import quantforge as qf

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "examples" / "data" / "synth_daily.csv"
OUT_DIR = ROOT / "results"

STRATEGIES = {
    "MA crossover (20,60)": ("ma_crossover", {"fast": 20, "slow": 60}),
    "Mean reversion (20, z=1.5)": ("mean_reversion", {"lookback": 20, "entry_z": 1.5, "exit_z": 0.5}),
    "Market maker (20, 1%)": ("market_maker", {"fair_lookback": 20, "band": 0.01, "max_inventory": 5}),
}
COSTS = {"slippage": {"kind": "fixed_bps", "bps": 1.0}, "fee": {"kind": "per_share", "fee": 0.005}}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-show", action="store_true", help="don't open the chart")
    args = ap.parse_args()

    df = pd.read_csv(DATA)
    df["ts"] = pd.to_datetime(df["date"]).astype("int64")
    print(f"quantforge {qf.__version__} | {len(df)} bars, {df.date.iloc[0]} to {df.date.iloc[-1]}")
    print("costs: 1 bp slippage + $0.005/share, initial cash $100,000\n")

    header = f"{'strategy':28} {'return':>8} {'sharpe':>7} {'sortino':>8} {'max dd':>7} {'hit':>6} {'fills':>6}"
    print(header)
    print("-" * len(header))
    results = {}
    for label, (name, params) in STRATEGIES.items():
        r = qf.run_backtest(df, name, params, seed=7, **COSTS)
        rep = r["report"]
        results[label] = r
        print(f"{label:28} {rep['total_return'] * 100:7.2f}% {rep['sharpe']:7.3f} {rep['sortino']:8.3f} "
              f"{rep['max_drawdown'] * 100:6.2f}% {rep['hit_rate'] * 100:5.1f}% {r['num_fills']:6d}")

    # Deterministic replay with randomized execution costs.
    rnd = {"slippage": {"kind": "random_bps", "min_bps": 1, "max_bps": 5},
           "latency": {"kind": "random", "min_ns": 0, "max_ns": 10_000_000}}
    a = qf.run_backtest(df, "mean_reversion", {"lookback": 20}, seed=123, **rnd)
    b = qf.run_backtest(df, "mean_reversion", {"lookback": 20}, seed=123, **rnd)
    c = qf.run_backtest(df, "mean_reversion", {"lookback": 20}, seed=999, **rnd)
    same = bool(np.array_equal(a["equity"], b["equity"]))
    differ = not np.array_equal(a["equity"], c["equity"])
    print(f"\nreplay check: same seed identical = {same}, different seed differs = {differ}")

    chart = save_chart(results)
    print(f"chart saved: {chart}")
    if not args.no_show and sys.platform == "win32":
        os.startfile(chart)  # noqa: S606 - opens the PNG in the default viewer

    return 0 if (same and differ) else 1


def save_chart(results: dict) -> Path:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 7), sharex=True,
                                   gridspec_kw={"height_ratios": [3, 1]})
    for label, r in results.items():
        t = pd.to_datetime(r["equity_ts"])
        eq = r["equity"]
        peak = np.maximum.accumulate(eq)
        ax1.plot(t, eq, lw=1.3, label=label)
        ax2.plot(t, (eq - peak) / peak * 100, lw=1.0)
    ax1.axhline(100_000, color="grey", ls="--", lw=0.8)
    ax1.set_title("QuantForge equity curves (750 synthetic daily bars)")
    ax1.set_ylabel("equity ($)")
    ax1.legend(loc="upper left")
    ax2.set_ylabel("drawdown %")
    ax2.set_xlabel("date")
    fig.tight_layout()

    OUT_DIR.mkdir(exist_ok=True)
    path = OUT_DIR / "equity_curves.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


if __name__ == "__main__":
    raise SystemExit(main())
