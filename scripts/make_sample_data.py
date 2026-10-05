#!/usr/bin/env python3
"""Generate or download sample OHLCV bar data for QuantForge examples.

By default this writes a reproducible synthetic daily series so the examples run
with no network access. Pass --source stooq to download real daily bars for a
ticker (requires the `pandas-datareader`-style Stooq CSV endpoint, no API key).

Examples
--------
    python scripts/make_sample_data.py --out examples/data/synth_daily.csv
    python scripts/make_sample_data.py --source stooq --symbol AAPL \
        --out examples/data/aapl_daily.csv
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import sys
import urllib.request


def gen_synthetic(n: int, seed: int) -> list[tuple[str, float, float, float, float, float]]:
    """Deterministic geometric-random-walk daily bars."""
    import random

    rng = random.Random(seed)
    rows = []
    price = 100.0
    day = dt.date(2020, 1, 1)
    made = 0
    while made < n:
        # Skip weekends to look like trading days.
        if day.weekday() < 5:
            drift = 0.0002
            shock = rng.gauss(0.0, 0.012)
            ret = drift + shock
            open_ = price
            close = max(1.0, price * (1.0 + ret))
            high = max(open_, close) * (1.0 + abs(rng.gauss(0.0, 0.004)))
            low = min(open_, close) * (1.0 - abs(rng.gauss(0.0, 0.004)))
            vol = round(1_000_000 * (1.0 + abs(rng.gauss(0.0, 0.3))))
            rows.append((day.isoformat(), round(open_, 4), round(high, 4),
                         round(low, 4), round(close, 4), float(vol)))
            price = close
            made += 1
        day += dt.timedelta(days=1)
    return rows


def download_stooq(symbol: str) -> list[tuple[str, float, float, float, float, float]]:
    """Download daily bars from Stooq's free CSV endpoint (no key)."""
    url = f"https://stooq.com/q/d/l/?s={symbol.lower()}.us&i=d"
    with urllib.request.urlopen(url, timeout=30) as resp:  # noqa: S310
        text = resp.read().decode("utf-8")
    rows = []
    reader = csv.DictReader(text.splitlines())
    for r in reader:
        try:
            rows.append((r["Date"], float(r["Open"]), float(r["High"]),
                         float(r["Low"]), float(r["Close"]),
                         float(r.get("Volume") or 0.0)))
        except (KeyError, ValueError):
            continue
    if not rows:
        raise SystemExit(f"no data returned for '{symbol}' from Stooq")
    return rows


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="examples/data/synth_daily.csv")
    ap.add_argument("--source", choices=["synthetic", "stooq"], default="synthetic")
    ap.add_argument("--symbol", default="AAPL")
    ap.add_argument("--n", type=int, default=1000, help="synthetic bar count")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args(argv)

    if args.source == "stooq":
        rows = download_stooq(args.symbol)
    else:
        rows = gen_synthetic(args.n, args.seed)

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["date", "open", "high", "low", "close", "volume"])
        w.writerows(rows)

    print(f"wrote {len(rows)} bars to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
