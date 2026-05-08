#!/usr/bin/env python3

import argparse
import csv
import math
import sys
from pathlib import Path
from statistics import median


def percentile(values: list[float], quantile: float) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return values[0]

    sorted_values = sorted(values)
    raw_index = (len(sorted_values) - 1) * quantile
    lower_index = int(math.floor(raw_index))
    upper_index = int(math.ceil(raw_index))

    if lower_index == upper_index:
        return sorted_values[lower_index]

    lower_value = sorted_values[lower_index]
    upper_value = sorted_values[upper_index]
    fraction = raw_index - lower_index
    return lower_value + (upper_value - lower_value) * fraction


def parse_input_csv(input_path: Path) -> list[dict[str, str]]:
    with input_path.open("r", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        rows = list(reader)
    return rows


def summarize(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[str, str], dict[str, list[float]]] = {}

    for row in rows:
        suite = row.get("suite", "").strip()
        benchmark = row.get("benchmark", "").strip()
        mode = row.get("mode", "").strip()
        elapsed_seconds = float(row.get("elapsed_seconds", "0") or 0)
        ratio_vs_baseline = float(row.get("ratio_vs_baseline", "0") or 0)

        group_key = (suite, benchmark)
        if group_key not in grouped:
            grouped[group_key] = {
                "baseline_elapsed_seconds": [],
                "perf_elapsed_seconds": [],
                "perf_ratio": [],
            }

        if mode == "baseline":
            grouped[group_key]["baseline_elapsed_seconds"].append(elapsed_seconds)
        elif mode == "perf":
            grouped[group_key]["perf_elapsed_seconds"].append(elapsed_seconds)
            grouped[group_key]["perf_ratio"].append(ratio_vs_baseline)

    summary_rows: list[dict[str, str]] = []
    for (suite, benchmark), values in sorted(grouped.items()):
        baseline_times = values["baseline_elapsed_seconds"]
        perf_times = values["perf_elapsed_seconds"]
        perf_ratios = values["perf_ratio"]
        run_count = min(len(baseline_times), len(perf_times))

        summary_rows.append(
            {
                "suite": suite,
                "benchmark": benchmark,
                "runs": str(run_count),
                "baseline_median_seconds": f"{median(baseline_times) if baseline_times else 0.0:.3f}",
                "perf_median_seconds": f"{median(perf_times) if perf_times else 0.0:.3f}",
                "perf_ratio_median": f"{median(perf_ratios) if perf_ratios else 0.0:.3f}",
                "perf_ratio_p95": f"{percentile(perf_ratios, 0.95):.3f}",
            }
        )

    return summary_rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize perf overhead experiment CSV files.")
    parser.add_argument(
        "--input-csv",
        action="append",
        required=True,
        help="Path to a perf_overhead.csv file. Pass multiple times to merge suites.",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Optional output CSV path (default: print only).",
    )
    args = parser.parse_args()

    all_rows: list[dict[str, str]] = []
    for input_csv in args.input_csv:
        input_path = Path(input_csv)
        if not input_path.exists():
            raise RuntimeError(f"Input CSV not found: {input_path}")
        all_rows.extend(parse_input_csv(input_path))

    summary_rows = summarize(all_rows)

    header = [
        "suite",
        "benchmark",
        "runs",
        "baseline_median_seconds",
        "perf_median_seconds",
        "perf_ratio_median",
        "perf_ratio_p95",
    ]

    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8", newline="") as output_file:
            writer = csv.DictWriter(output_file, fieldnames=header)
            writer.writeheader()
            writer.writerows(summary_rows)
        print(f"Wrote summary: {output_path}")

    writer = csv.DictWriter(sys.stdout, fieldnames=header)
    writer.writeheader()
    writer.writerows(summary_rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
