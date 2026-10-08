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
INJECTION_COLUMNS = (
    "l1_injection_kw",
    "l2_injection_kw",
    "l3_injection_kw",
)


def load_power_data(csv_path: Path):
    timestamps = []
    series_columns = (*PHASE_COLUMNS, *INJECTION_COLUMNS)
    series_values = {column: [] for column in series_columns}
    total_values = []
    skipped_rows = 0

    with csv_path.open("r", newline="", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        required_columns = {"timestamp", *series_columns}
        missing_columns = required_columns.difference(reader.fieldnames or [])
        if missing_columns:
            missing = ", ".join(sorted(missing_columns))
            raise ValueError(f"ontbrekende CSV-kolommen: {missing}")

        for row in reader:
            try:
                timestamp = datetime.fromisoformat(row["timestamp"])
                values = {column: float(row[column]) for column in series_columns}
                if not all(math.isfinite(value) for value in values.values()):
                    raise ValueError("niet-eindige meetwaarde")
            except (TypeError, ValueError):
                skipped_rows += 1
                continue

            timestamps.append(timestamp)
            total_values.append(sum(values[column] for column in PHASE_COLUMNS))
            for column, value in values.items():
                series_values[column].append(value)

    if not timestamps:
        raise ValueError("geen geldige meetregels gevonden")

    return timestamps, series_values, total_values, skipped_rows


def integrate_energy(timestamps, series_values, columns):
    energy_kwh = {column: 0.0 for column in columns}
    for index in range(1, len(timestamps)):
        elapsed_seconds = (timestamps[index] - timestamps[index - 1]).total_seconds()
        if not 0 < elapsed_seconds <= 5:
            continue

        for column in columns:
            previous = series_values[column][index - 1]
            current = series_values[column][index]
            if previous >= 0 and current >= 0:
                energy_kwh[column] += (
                    (previous + current) * 0.5 * elapsed_seconds / 3600
                )
    return energy_kwh


def render_chart(csv_path: Path, output_path: Path):
    timestamps, series_values, total_values, skipped_rows = load_power_data(csv_path)
    consumption_kwh = integrate_energy(timestamps, series_values, PHASE_COLUMNS)
    injection_kwh = integrate_energy(timestamps, series_values, INJECTION_COLUMNS)

    figure, (axis, energy_axis) = plt.subplots(
        2,
        1,
        figsize=(12, 7),
        gridspec_kw={"height_ratios": (6, 1)},
        layout="constrained",
    )
    phase_labels = {"l1": "Fase 1", "l2": "Fase 2", "l3": "Fase 3"}
    for column, color in zip(PHASE_COLUMNS, ("red", "green", "blue")):
        phase = column[:2]
        axis.plot(
            timestamps,
            series_values[column],
            color=color,
            linewidth=1,
            alpha=0.8,
            label=phase_labels[phase],
        )

    axis.fill_between(
        timestamps,
        0,
        series_values["l3_injection_kw"],
        color="orange",
        alpha=0.3,
        label="Fase 3 teruglevering",
        zorder=6,
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
    axis.legend(loc="upper left", frameon=False, ncols=5)

    total_consumption_kwh = sum(consumption_kwh.values())
    total_injection_kwh = sum(injection_kwh.values())
    energy_axis.axis("off")
    energy_axis.text(
        0.01,
        0.72,
        "Afname vandaag (kWh): "
        + "  ".join(
            f"L{phase + 1} {consumption_kwh[column]:.3f}"
            for phase, column in enumerate(PHASE_COLUMNS)
        )
        + f"  Totaal {total_consumption_kwh:.3f}",
        transform=energy_axis.transAxes,
        fontsize=10,
        family="monospace",
    )
    energy_axis.text(
        0.01,
        0.22,
        "Injectie vandaag (kWh): "
        + "  ".join(
            f"L{phase + 1} {injection_kwh[column]:.3f}"
            for phase, column in enumerate(INJECTION_COLUMNS)
        )
        + f"  Totaal {total_injection_kwh:.3f}",
        transform=energy_axis.transAxes,
        fontsize=10,
        family="monospace",
    )

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