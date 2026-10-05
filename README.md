# QuantForge

Event-driven backtesting & trading simulator with a C++20 core and Python bindings.

> Full documentation (architecture diagram, benchmark table, design tradeoffs,
> and limitations) is added in a later phase. This is a placeholder so the
> package builds.

## Quickstart (Python)

```python
import numpy as np
import quantforge as qf

n = 300
ts = (np.arange(n) * 86_400_000_000_000).astype(np.int64)  # daily, nanoseconds
close = 100 + np.cumsum(np.random.default_rng(0).normal(0, 1, n))
bars = {"ts": ts, "open": close, "high": close + 1, "low": close - 1,
        "close": close, "volume": np.full(n, 1e6)}

res = qf.run_backtest(bars, "ma_crossover", {"fast": 10, "slow": 30},
                      slippage={"kind": "fixed_bps", "bps": 1.0},
                      fee={"kind": "per_share", "fee": 0.005}, seed=42)
print(res["final_equity"], res["report"]["sharpe"])
```
