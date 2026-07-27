#!/usr/bin/env python3
from __future__ import annotations

import csv
import math
import statistics
import sys
from pathlib import Path

import matplotlib.pyplot as plt


def percentile(values: list[float], percent: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return math.nan
    position = (len(ordered) - 1) * percent / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def read_latencies(path: Path, field: str = "latency_seconds") -> tuple[list[float], list[str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    latencies = [float(row[field]) for row in rows if row[field]]
    codes = [row["http_code"] for row in rows]
    return latencies, codes


def main() -> int:
    baseline_path = Path(sys.argv[1])
    load_path = Path(sys.argv[2])
    telemetry_path = Path(sys.argv[3])
    output_dir = load_path.parent

    baseline, baseline_codes = read_latencies(baseline_path)
    loaded, loaded_codes = read_latencies(load_path)
    successful = sum(code == "200" for code in loaded_codes)

    baseline_mean = statistics.fmean(baseline)
    loaded_mean = statistics.fmean(loaded)
    increase_ms = (loaded_mean - baseline_mean) * 1000.0
    increase_percent = ((loaded_mean / baseline_mean) - 1.0) * 100.0 if baseline_mean else math.nan

    report = output_dir / "load_test_summary.txt"
    report.write_text(
        "\n".join(
            [
                f"baseline_requests={len(baseline)}",
                f"load_requests={len(loaded)}",
                f"successful_http_200={successful}",
                f"failed_requests={len(loaded) - successful}",
                f"baseline_mean_ms={baseline_mean * 1000.0:.3f}",
                f"load_mean_ms={loaded_mean * 1000.0:.3f}",
                f"load_p50_ms={percentile(loaded, 50) * 1000.0:.3f}",
                f"load_p95_ms={percentile(loaded, 95) * 1000.0:.3f}",
                f"load_p99_ms={percentile(loaded, 99) * 1000.0:.3f}",
                f"mean_latency_increase_ms={increase_ms:.3f}",
                f"mean_latency_increase_percent={increase_percent:.3f}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    plt.figure(figsize=(10, 6))
    plt.hist([value * 1000.0 for value in loaded], bins=40)
    plt.xlabel("Response latency (ms)")
    plt.ylabel("Number of requests")
    plt.title("Test 2-3: telemetry response latency with 50 concurrent curl loops")
    plt.tight_layout()
    latency_plot = output_dir / "latency_histogram.png"
    plt.savefig(latency_plot, dpi=200)
    plt.close()

    with telemetry_path.open(newline="", encoding="utf-8") as handle:
        telemetry = list(csv.DictReader(handle))
    times = [float(row["time_s"]) for row in telemetry]
    cpu = [float(row["cpu_usage_percent"]) for row in telemetry]
    memory_mb = [float(row["memory_available_kb"]) / 1024.0 for row in telemetry]
    temperatures = [
        float(row["temperature_c"]) if row["temperature_c"] else math.nan
        for row in telemetry
    ]

    plt.figure(figsize=(10, 6))
    plt.plot(times, cpu, marker=".", label="CPU usage (%)")
    plt.plot(times, memory_mb, marker=".", label="Available memory (MB)")
    if any(not math.isnan(value) for value in temperatures):
        plt.plot(times, temperatures, marker=".", label="CPU temperature (°C)")
    plt.xlabel("Time (seconds)")
    plt.ylabel("Measured value")
    plt.title("Test 2-3: telemetry changes during concurrent requests")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    telemetry_plot = output_dir / "telemetry_during_load.png"
    plt.savefig(telemetry_plot, dpi=200)
    plt.close()

    print(f"Created {report}")
    print(f"Created {latency_plot}")
    print(f"Created {telemetry_plot}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
