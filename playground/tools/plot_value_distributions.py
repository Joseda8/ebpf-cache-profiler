#!/usr/bin/env python3

import argparse
import csv
import math
import re
from pathlib import Path
from statistics import median

try:
    import matplotlib.pyplot as plt
except ModuleNotFoundError as import_error:
    raise SystemExit(
        "Missing dependency: matplotlib. Install it with:\n"
        "  python3 -m pip install matplotlib"
    ) from import_error


PERF_EVENT_MAP = {
    "L1-dcache-loads": "l1_access",
    "L1-dcache-load-misses": "l1_miss",
    "l2_rqsts.references:u": "l2_access",
    "l2_rqsts.miss:u": "l2_miss",
    "longest_lat_cache.reference:u": "llc_access",
    "longest_lat_cache.miss:u": "llc_miss",
}

METRICS = [
    ("l1_access", "L1 Accesses"),
    ("l1_miss", "L1 Misses"),
    ("l2_access", "L2 Accesses"),
    ("l2_miss", "L2 Misses"),
    ("llc_access", "LLC Accesses"),
    ("llc_miss", "LLC Misses"),
]


def parse_perf_log(perf_log_path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    with perf_log_path.open("r", encoding="utf-8") as perf_file:
        for line in perf_file:
            parts = line.strip().split(";")
            if len(parts) < 3:
                continue
            raw_count = parts[0].strip()
            event_name = parts[2].strip()
            if event_name not in PERF_EVENT_MAP:
                continue
            if raw_count == "<not counted>" or raw_count == "":
                values[PERF_EVENT_MAP[event_name]] = 0
            else:
                values[PERF_EVENT_MAP[event_name]] = int(raw_count.replace(",", ""))
    return values


def parse_profiler_csv(profiler_csv_path: Path) -> dict[str, int]:
    with profiler_csv_path.open("r", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        rows = list(reader)
    if not rows:
        raise RuntimeError(f"No sample rows in {profiler_csv_path}")
    last = rows[-1]
    return {
        "l1_access": int(last["l1_read_access_total"]),
        "l1_miss": int(last["l1_read_miss_total"]),
        "l2_access": int(last["l2_read_access_total"]),
        "l2_miss": int(last["l2_read_miss_total"]),
        "llc_access": int(last["llc_read_access_total"]),
        "llc_miss": int(last["llc_read_miss_total"]),
    }


def collect_runs(results_dir: Path) -> tuple[dict[str, list[int]], dict[str, list[int]]]:
    perf_values: dict[str, list[int]] = {metric_key: [] for metric_key, _ in METRICS}
    profiler_values: dict[str, list[int]] = {metric_key: [] for metric_key, _ in METRICS}

    perf_logs = sorted(results_dir.glob("perf_stat_run_*.log"))
    if not perf_logs:
        raise RuntimeError(f"No perf logs found in {results_dir}")

    run_pattern = re.compile(r"perf_stat_run_(\d+)\.log$")
    for perf_log in perf_logs:
        match = run_pattern.search(perf_log.name)
        if not match:
            continue
        run_idx = int(match.group(1))
        profiler_csv = results_dir / f"profiler_samples_run_{run_idx}.csv"
        if not profiler_csv.exists():
            continue

        perf_row = parse_perf_log(perf_log)
        profiler_row = parse_profiler_csv(profiler_csv)
        for metric_key, _ in METRICS:
            perf_values[metric_key].append(perf_row.get(metric_key, 0))
            profiler_values[metric_key].append(profiler_row.get(metric_key, 0))

    return perf_values, profiler_values


def make_plot(results_dir: Path, output_png: Path, log_scale: bool) -> None:
    perf_values, profiler_values = collect_runs(results_dir)

    fig, axes = plt.subplots(3, 2, figsize=(14, 12))
    axes_flat = axes.flatten()

    for axis_idx, (metric_key, metric_label) in enumerate(METRICS):
        ax = axes_flat[axis_idx]
        perf_series = perf_values[metric_key]
        profiler_series = profiler_values[metric_key]

        ax.boxplot(
            [perf_series, profiler_series],
            labels=["perf", "profiler"],
            showfliers=True,
        )
        ax.set_title(metric_label)
        ax.set_ylabel("count")
        if log_scale:
            ax.set_yscale("symlog", linthresh=1.0)

        if perf_series and profiler_series:
            perf_med = median(perf_series)
            profiler_med = median(profiler_series)
            rel_err = 0.0
            if perf_med > 0:
                rel_err = abs(profiler_med - perf_med) * 100.0 / perf_med
            ax.text(
                0.03,
                0.95,
                f"med perf={perf_med:.0f}\nmed profiler={profiler_med:.0f}\nmed rel err={rel_err:.2f}%",
                transform=ax.transAxes,
                va="top",
                fontsize=9,
                bbox={"facecolor": "white", "alpha": 0.8, "edgecolor": "none"},
            )

    fig.suptitle(f"Perf vs Profiler Value Distributions ({results_dir.name})", fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(output_png, dpi=150)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot perf vs profiler value distributions for one experiment result directory.")
    parser.add_argument("--results-dir", required=True, help="Path to playground results directory (e.g., playground/results/threaded_vs_profiler)")
    parser.add_argument("--output", default=None, help="Output PNG path (default: <results-dir>/value_distributions.png)")
    parser.add_argument("--log-scale", action="store_true", help="Use symlog scale on Y axes.")
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    if not results_dir.exists():
        raise RuntimeError(f"Results directory not found: {results_dir}")

    output_png = Path(args.output) if args.output else results_dir / "value_distributions.png"
    make_plot(results_dir, output_png, args.log_scale)
    print(f"Wrote plot: {output_png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
