#!/usr/bin/env python3
"""Render a PNG chart from a daily Powerlog CSV file."""

import argparse
import csv
import math
import sys
from datetime import datetime
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt


PHASE_COLUMNS = (
    "l1_consumption_kw",
    "l2_consumption_kw",
    "l3_consumption_kw",
)


def load_power_data(csv_path: Path):
    timestamps = []
    phase_values = {column: [] for column in PHASE_COLUMNS}
    total_values = []
    skipped_rows = 0

    with csv_path.open("r", newline="", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        required_columns = {"timestamp", *PHASE_COLUMNS}
        missing_columns = required_columns.difference(reader.fieldnames or [])
        if missing_columns:
            missing = ", ".join(sorted(missing_columns))
            raise ValueError(f"ontbrekende CSV-kolommen: {missing}")

        for row in reader:
            try:
                timestamp = datetime.fromisoformat(row["timestamp"])
                phases = [float(row[column]) for column in PHASE_COLUMNS]
                if not all(math.isfinite(value) for value in phases):
                    raise ValueError("niet-eindige meetwaarde")
            except (TypeError, ValueError):
                skipped_rows += 1
                continue

            timestamps.append(timestamp)
            total_values.append(sum(phases))
            for column, value in zip(PHASE_COLUMNS, phases):
                phase_values[column].append(value)

    if not timestamps:
        raise ValueError("geen geldige meetregels gevonden")

    return timestamps, phase_values, total_values, skipped_rows


def render_chart(csv_path: Path, output_path: Path):
    timestamps, phase_values, total_values, skipped_rows = load_power_data(csv_path)

    figure, axis = plt.subplots(figsize=(12, 6), layout="constrained")
    phase_labels = {"l1": "Fase 1", "l2": "Fase 2", "l3": "Fase 3"}
    for column, color in zip(PHASE_COLUMNS, ("#d97706", "#15803d", "#2563eb")):
        phase = column[:2]
        axis.plot(
            timestamps,
            phase_values[column],
            color=color,
            linewidth=1,
            alpha=0.8,
            label=phase_labels[phase],
        )

    axis.plot(
        timestamps,
        total_values,
        color="#172554",
        linewidth=2.2,
        label="Totaal",
        zorder=5,
    )
    axis.set_title(f"Powerverbruik - {csv_path.stem}")
    axis.set_xlabel("Tijd")
    axis.set_ylabel("Vermogen (kW)")
    axis.grid(True, color="#cbd5e1", linewidth=0.7, alpha=0.7)
    axis.legend(loc="best", frameon=False, ncols=4)

    if timestamps[0].date() == timestamps[-1].date():
        axis.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    else:
        axis.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d\n%H:%M"))

    figure.savefig(output_path, dpi=160)
    plt.close(figure)
    return len(timestamps), skipped_rows


def main():
    parser = argparse.ArgumentParser(
        description="Maak een PNG-grafiek van powerlog-meetwaarden.",
        epilog="Voorbeeld: python3 plot_powerlog.py build/logs/2026-10-04.csv",
    )
    parser.add_argument("csv_file", type=Path, help="dagelijkse Powerlog CSV")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="PNG-bestand (standaard: dezelfde naam als de CSV)",
    )
    args = parser.parse_args()
    output_path = args.output or args.csv_file.with_suffix(".png")

    try:
        row_count, skipped_rows = render_chart(args.csv_file, output_path)
    except (OSError, ValueError) as error:
        print(f"fout: {error}", file=sys.stderr)
        return 1

    print(f"PNG opgeslagen: {output_path} ({row_count} meetregels)")
    if skipped_rows:
        print(f"waarschuwing: {skipped_rows} ongeldige regels overgeslagen", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())