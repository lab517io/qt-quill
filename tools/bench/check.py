#!/usr/bin/env python3
"""Compares quill-bench JSON output with tools/bench/budgets.json.

Usage: check.py results.json [budgets.json] [--slack 3]
Prints one line per budget; exits 1 when a budget is exceeded and --strict
is given (CI only reports).
"""
import argparse
import json
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument("results")
parser.add_argument("budgets", nargs="?", default=os.path.join(os.path.dirname(__file__), "budgets.json"))
parser.add_argument("--slack", type=float, default=1.0, help="factor for absolute (ms) budgets")
parser.add_argument("--strict", action="store_true")
args = parser.parse_args()

results = {(r["corpus"], r["scenario"]): r for r in json.load(open(args.results))}
failed = 0
for b in json.load(open(args.budgets))["budgets"]:
    r = results.get((b["corpus"], b["scenario"]))
    name = f'{b["corpus"]}/{b["scenario"]} {b["stat"]}'
    if r is None:
        print(f"  skip  {name}: not measured")
        continue
    value = r[b["stat"]]
    if "ratio" in b:
        base = results.get((b["corpus"], b["baseline"]))
        if base is None:
            print(f"  skip  {name}: baseline {b['baseline']} not measured")
            continue
        ratio = value / max(base[b["stat"]], 1e-6)
        ok = ratio <= b["ratio"]
        detail = f"{value:.2f} ms = x{ratio:.2f} of {b['baseline']} (budget x{b['ratio']})"
    else:
        limit = b["ms"] * args.slack
        ok = value <= limit
        detail = f"{value:.2f} ms (budget {limit:g} ms)"
    failed += not ok
    print(f"  {'ok  ' if ok else 'OVER'}  {name}: {detail}")
print(f"{failed} budget(s) exceeded" if failed else "all budgets met")
sys.exit(1 if failed and args.strict else 0)
