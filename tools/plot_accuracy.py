#!/usr/bin/env python3
"""Plot the output of `legacy_accuracy --csv` (docs/figures/legacy_vs_fp32.png).

    build/legacy_accuracy 100000 --csv > accuracy.csv
    python3 tools/plot_accuracy.py accuracy.csv docs/figures/legacy_vs_fp32.png
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

src = sys.argv[1] if len(sys.argv) > 1 else "accuracy.csv"
dst = sys.argv[2] if len(sys.argv) > 2 else "docs/figures/legacy_vs_fp32.png"

rows = list(csv.DictReader(open(src)))
gaps = [int(r["exponent_gap"]) for r in rows]
pct = lambda key: [100.0 * int(r[key]) / int(r["total"]) for r in rows]  # noqa: E731
legacy_exact, legacy_1ulp, legacy_worse, fp32_exact = (pct(k) for k in ("legacy_exact", "legacy_1ulp", "legacy_worse", "fp32_exact"))

fig, ax = plt.subplots(figsize=(9, 4.4), dpi=130)
x = range(len(gaps))
ax.bar(x, legacy_exact, color="#93c5fd", label="legacy adder — correctly rounded")
ax.bar(x, legacy_1ulp, bottom=legacy_exact, color="#fbbf24", label="legacy adder — 1 ulp off")
ax.bar(x, legacy_worse, bottom=[a + b for a, b in zip(legacy_exact, legacy_1ulp)], color="#ef4444", label="legacy adder — > 1 ulp off")
ax.plot(list(x), fp32_exact, "o-", color="#065f46", lw=2, ms=5, label="fp32::add — correctly rounded")
ax.set_xticks(list(x))
ax.set_xticklabels([str(g) for g in gaps])
ax.set_xlabel("exponent difference between the operands")
ax.set_ylabel("share of random operand pairs (%)")
ax.set_ylim(0, 105)
ax.set_title(f"Correctly rounded results: original program vs. fp32 ({int(rows[0]['total']):,} random pairs per bar)")
ax.grid(axis="y", alpha=0.3)
ax.legend(loc="lower left", fontsize=8, framealpha=0.95)
fig.tight_layout()
fig.savefig(dst)
print(f"wrote {dst}")
