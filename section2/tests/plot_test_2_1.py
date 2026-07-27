#!/usr/bin/env python3
from __future__ import annotations

import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def main() -> int:
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    modes = [
        ("idle", "Idle"),
        ("stream", "Stream only"),
        ("stream_detection", "Stream + detection"),
    ]

    maximum_rows: list[tuple[str, float]] = []
    plt.figure(figsize=(10, 6))
    for filename, label in modes:
        path = directory / f"{filename}.csv"
        if not path.exists():
            raise SystemExit(f"Missing {path}")
        rows = load_csv(path)
        times = [float(row["time_s"]) / 60.0 for row in rows]
        temperatures = [float(row["temperature_c"]) for row in rows if row["temperature_c"]]
        paired_times = [
            float(row["time_s"]) / 60.0 for row in rows if row["temperature_c"]
        ]
        if not temperatures:
            raise SystemExit(f"No temperature samples in {path}")
        plt.plot(paired_times, temperatures, marker="o", label=label)
        maximum_rows.append((label, max(temperatures)))

    plt.xlabel("Time (minutes)")
    plt.ylabel("CPU temperature (°C)")
    plt.title("Test 2-1: CPU temperature in three operating modes")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    output_plot = directory / "temperature_three_modes.png"
    plt.savefig(output_plot, dpi=200)
    plt.close()

    output_table = directory / "maximum_temperature.csv"
    with output_table.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["mode", "maximum_temperature_c"])
        writer.writerows((label, f"{value:.3f}") for label, value in maximum_rows)

    print(f"Created {output_plot}")
    print(f"Created {output_table}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
