"""QuantForge: event-driven backtesting & trading simulator.

This package wraps the compiled C++ core (``quantforge._core``) with a thin,
pandas-friendly Python layer. The heavy lifting (the event loop, execution
models, portfolio accounting, analytics) runs in C++; Python is used only to
marshal data in and plot results out.
"""

from __future__ import annotations

from typing import Any, Mapping, Optional

import numpy as np

from . import _core
from ._core import Side, SignalDirection, max_drawdown, sharpe

__all__ = [
    "Side",
    "SignalDirection",
    "run_backtest",
    "sharpe",
    "max_drawdown",
    "__version__",
]

__version__ = _core.__version__


def run_backtest(
    bars: Any,
    strategy: str,
    strategy_params: Optional[Mapping[str, Any]] = None,
    *,
    latency: Optional[Mapping[str, Any]] = None,
    slippage: Optional[Mapping[str, Any]] = None,
    fee: Optional[Mapping[str, Any]] = None,
    execution_cfg: Optional[Mapping[str, Any]] = None,
    risk: Optional[Mapping[str, Any]] = None,
    initial_cash: float = 100_000.0,
    target_position: float = 100.0,
    periods_per_year: float = 252.0,
    seed: int = 0,
) -> dict:
    """Run one backtest and return results as a dict of NumPy arrays + metrics.

    Parameters
    ----------
    bars:
        Either a pandas DataFrame with columns ``open, high, low, close,
        volume`` (and a DatetimeIndex or an integer ``ts``/``timestamp``
        column), or a mapping of those column names to array-likes.
    strategy:
        One of ``"ma_crossover"``, ``"mean_reversion"``, ``"market_maker"``.
    strategy_params:
        Strategy-specific keyword values, e.g. ``{"fast": 10, "slow": 30}``.
    latency, slippage, fee:
        Cost-model specs, e.g. ``{"kind": "fixed_bps", "bps": 1.0}``.
    seed:
        RNG seed. A fixed seed makes the run exactly reproducible.

    Returns
    -------
    dict with keys ``equity_ts``, ``equity`` (NumPy arrays), ``trade_pnl``,
    ``trade_notional``, a nested ``report`` dict, and scalar run statistics.
    """
    ts, o, h, l, c, v = _extract_ohlcv(bars)

    return _core.run_backtest(
        ts=np.asarray(ts, dtype=np.int64),
        open=np.asarray(o, dtype=np.float64),
        high=np.asarray(h, dtype=np.float64),
        low=np.asarray(l, dtype=np.float64),
        close=np.asarray(c, dtype=np.float64),
        volume=np.asarray(v, dtype=np.float64),
        strategy=strategy,
        strategy_params=dict(strategy_params or {}),
        latency=dict(latency or {}),
        slippage=dict(slippage or {}),
        fee=dict(fee or {}),
        execution_cfg=dict(execution_cfg or {}),
        risk=dict(risk or {}),
        initial_cash=initial_cash,
        target_position=target_position,
        periods_per_year=periods_per_year,
        seed=seed,
    )


def _extract_ohlcv(bars: Any):
    """Pulls (ts, open, high, low, close, volume) array-likes out of `bars`."""
    # pandas DataFrame path (import lazily so pandas stays optional).
    cols = None
    try:
        import pandas as pd  # noqa: F401

        if isinstance(bars, pd.DataFrame):
            cols = bars
    except Exception:  # pragma: no cover - pandas not installed
        pass

    if cols is not None:
        df = cols
        if "ts" in df.columns:
            ts = df["ts"].to_numpy()
        elif "timestamp" in df.columns:
            ts = df["timestamp"].to_numpy()
        else:
            # Use the index; convert datetimes to integer nanoseconds.
            idx = df.index
            try:
                ts = idx.view("int64")
            except (TypeError, ValueError):
                ts = np.arange(len(df), dtype=np.int64)
        return (
            ts,
            df["open"].to_numpy(),
            df["high"].to_numpy(),
            df["low"].to_numpy(),
            df["close"].to_numpy(),
            df["volume"].to_numpy(),
        )

    # Mapping path.
    m = bars
    ts = m.get("ts", m.get("timestamp"))
    if ts is None:
        ts = np.arange(len(m["close"]), dtype=np.int64)
    return (ts, m["open"], m["high"], m["low"], m["close"], m["volume"])
