#!/usr/bin/env python3
from __future__ import annotations

import csv
import statistics
import sys
from pathlib import Path

import matplotlib.pyplot as plt


def linear_slope(x: list[float], y: list[float]) -> float:
    x_mean = statistics.fmean(x)
    y_mean = statistics.fmean(y)
    denominator = sum((value - x_mean) ** 2 for value in x)
    if denominator == 0:
        return 0.0
    return sum((a - x_mean) * (b - y_mean) for a, b in zip(x, y)) / denominator


def main() -> int:
    path = Path(sys.argv[1])
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    times_minutes = [float(row["time_s"]) / 60.0 for row in rows]
    rss_kb = [float(row["vmrss_kb"]) for row in rows]

    plt.figure(figsize=(10, 6))
    plt.plot(times_minutes, rss_kb, marker=".")
    plt.xlabel("Time (minutes)")
    plt.ylabel("C process RSS (kB)")
    plt.title("Test 2-2: C process memory during continuous streaming")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plot_path = path.with_name("c_process_memory.png")
    plt.savefig(plot_path, dpi=200)
    plt.close()

    slope_kb_per_minute = linear_slope(times_minutes, rss_kb)
    growth_kb = rss_kb[-1] - rss_kb[0]
    peak_kb = max(rss_kb)
    threshold = max(1024.0, rss_kb[0] * 0.10)
    likely_leak = growth_kb > threshold and slope_kb_per_minute > 256.0

    analysis_path = path.with_name("memory_leak_analysis.txt")
    analysis_path.write_text(
        "\n".join(
            [
                f"first_rss_kb={rss_kb[0]:.1f}",
                f"last_rss_kb={rss_kb[-1]:.1f}",
                f"peak_rss_kb={peak_kb:.1f}",
                f"growth_kb={growth_kb:.1f}",
                f"linear_slope_kb_per_minute={slope_kb_per_minute:.3f}",
                f"heuristic_result={'possible memory leak' if likely_leak else 'no sustained leak pattern detected'}",
                "Note: confirm the conclusion with the curve shape and a longer run if growth is borderline.",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"Created {plot_path}")
    print(f"Created {analysis_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
