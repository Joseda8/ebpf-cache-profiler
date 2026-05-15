#!/usr/bin/env python3

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns


RESULT_SET_FOLDERS = [
    "ebpf_overhead_local_workloads",
    "perf_overhead_local_workloads",
    "ebpf_overhead_pyperformance",
    "perf_overhead_pyperformance",
    "ebpf_overhead_npb",
    "perf_overhead_npb",
]

RESULT_FOLDER_PAIRS = [
    ("ebpf_overhead_local_workloads", "perf_overhead_local_workloads"),
    ("ebpf_overhead_pyperformance", "perf_overhead_pyperformance"),
    ("ebpf_overhead_npb", "perf_overhead_npb"),
]
TARGET_FILENAMES = {"profiler_stats.csv", "raw_process_metrics.csv"}

PROFILER_STATS_FIELDS = {
    "elapsed_ms",
    "l1_read_access_total",
    "l1_read_miss_total",
    "l2_read_access_total",
    "l2_read_miss_total",
    "llc_read_access_total",
    "llc_read_miss_total",
}

MISS_RATE_METRICS = {
    "l1_miss_rate": ("l1_read_miss_total", "l1_read_access_total"),
    "l2_miss_rate": ("l2_read_miss_total", "l2_read_access_total"),
    "llc_miss_rate": ("llc_read_miss_total", "llc_read_access_total"),
}

PLOT_METRIC_ORDER = [
    "elapsed_s",
    "l1_read_access_total",
    "l1_read_miss_total",
    "l2_read_access_total",
    "l2_read_miss_total",
    "llc_read_access_total",
    "llc_read_miss_total",
    "l1_miss_rate",
    "l2_miss_rate",
    "llc_miss_rate",
]

METRIC_UNIT_BY_NAME = {
    "elapsed_s": "seconds",
    "l1_read_access_total": "count",
    "l1_read_miss_total": "count",
    "l2_read_access_total": "count",
    "l2_read_miss_total": "count",
    "llc_read_access_total": "count",
    "llc_read_miss_total": "count",
    "l1_miss_rate": "ratio",
    "l2_miss_rate": "ratio",
    "llc_miss_rate": "ratio",
}

BACKEND_ORDER = ["ebpf", "perf"]


def format_metric_value(metric_name: str, value: float) -> str:
    # Format annotation values by metric scale
    if metric_name.endswith("_miss_rate"):
        return f"{value:.6f}"
    if metric_name == "elapsed_s":
        return f"{value:.2f}"
    return f"{value:.3e}"


def annotate_metric_medians(plot_axis: plt.Axes, df_metric: pd.DataFrame, metric_name: str) -> None:
    # Compute medians and add a summary annotation in axis corner
    median_by_backend = df_metric.groupby("profiler_family", observed=True)["value"].median()
    median_ebpf = float(median_by_backend["ebpf"])
    median_perf = float(median_by_backend["perf"])
    ratio_ebpf_perf = median_ebpf / median_perf
    percent_diff = ((median_ebpf - median_perf) / median_perf) * 100.0

    annotation_text = (
        f"median_ebpf: {format_metric_value(metric_name, median_ebpf)}\n"
        f"median_perf: {format_metric_value(metric_name, median_perf)}\n"
        f"ratio(ebpf/perf): {ratio_ebpf_perf:.6f}\n"
        f"%diff vs perf: {percent_diff:+.3f}%"
    )
    if metric_name.endswith("_miss_rate"):
        percentage_point_diff = (median_ebpf - median_perf) * 100.0
        annotation_text = annotation_text + f"\npp_diff: {percentage_point_diff:+.4f} pp"

    plot_axis.text(
        0.02,
        0.98,
        annotation_text,
        transform=plot_axis.transAxes,
        ha="left",
        va="top",
        fontsize=9,
        bbox={"boxstyle": "round,pad=0.3", "facecolor": "white", "alpha": 0.85, "edgecolor": "gray"},
    )


def load_result_dataframes(results_root: Path, result_folder_name: str) -> tuple[pd.DataFrame, pd.DataFrame]:
    # Compute results folder for the given batch
    result_folder_path = results_root / result_folder_name
    stats_frames = []
    raw_metrics_frames = []

    # Extract files of interest
    path_files_raw_data = sorted(
        pCsvPath
        for pCsvPath in result_folder_path.rglob("*.csv")
        if pCsvPath.name in TARGET_FILENAMES
    )

    # Load each file and group by CSV type
    for path_file_raw_data in path_files_raw_data:
        path_relative_raw_data = path_file_raw_data.relative_to(results_root)
        print(f"  - {path_relative_raw_data}")
        df_file = pd.read_csv(path_file_raw_data)
        df_file.insert(0, "source_file", str(path_relative_raw_data))

        if path_file_raw_data.name == "profiler_stats.csv":
            stats_frames.append(df_file)
        elif path_file_raw_data.name == "raw_process_metrics.csv":
            raw_metrics_frames.append(df_file)

    # Join stats and keep only profiler metrics of interest
    df_stats = pd.concat(stats_frames, ignore_index=True)
    df_stats = df_stats[df_stats["metric"].isin(PROFILER_STATS_FIELDS)]

    # Join raw process metrics
    df_raw_metrics = pd.concat(raw_metrics_frames, ignore_index=True)

    return df_stats, df_raw_metrics


def prepare_df_stats(df_stats_combined: pd.DataFrame) -> pd.DataFrame:
    # Cast numeric fields
    df_stats_prepared = df_stats_combined.copy()
    df_stats_prepared["run"] = pd.to_numeric(df_stats_prepared["run"], errors="raise").astype(int)
    df_stats_prepared["value"] = pd.to_numeric(df_stats_prepared["value"], errors="raise").astype(float)

    # Convert elapsed time from milliseconds to seconds
    elapsed_rows = df_stats_prepared["metric"] == "elapsed_ms"
    df_stats_prepared.loc[elapsed_rows, "value"] = df_stats_prepared.loc[elapsed_rows, "value"] / 1000.0
    df_stats_prepared.loc[elapsed_rows, "metric"] = "elapsed_s"
    df_stats_prepared.loc[elapsed_rows, "unit"] = "seconds"

    # Derive workload and profiler family from source_file
    source_parts = df_stats_prepared["source_file"].str.split("/", expand=True)
    df_stats_prepared["result_folder_name"] = source_parts[0]
    df_stats_prepared["workload"] = source_parts[1]
    df_stats_prepared["profiler_family"] = df_stats_prepared["result_folder_name"].str.extract(r"^(ebpf|perf)_")[0]

    return df_stats_prepared


def append_miss_rate_metrics(df_stats_prepared: pd.DataFrame) -> pd.DataFrame:
    # Pivot raw counters by metric
    pivot_index_columns = ["source_file", "run", "backend", "workload", "profiler_family"]
    df_pivot = df_stats_prepared.pivot(index=pivot_index_columns, columns="metric", values="value")

    for miss_rate_name, metric_names in MISS_RATE_METRICS.items():
        miss_metric_name, access_metric_name = metric_names
        df_pivot[miss_rate_name] = df_pivot[miss_metric_name] / df_pivot[access_metric_name]

    # Convert miss-rate fields back to long format
    miss_rate_columns = list(MISS_RATE_METRICS.keys())
    df_miss_rates = df_pivot[miss_rate_columns].reset_index().melt(
        id_vars=pivot_index_columns,
        var_name="metric",
        value_name="value",
    )
    df_miss_rates["unit"] = "ratio"
    df_miss_rates["source"] = "derived"

    # Join base and derived metrics into one plotting frame
    df_stats_with_miss_rates = pd.concat([df_stats_prepared, df_miss_rates], ignore_index=True)
    return df_stats_with_miss_rates


def generate_violin_plots(df_stats_with_miss_rates: pd.DataFrame, output_dir: Path) -> None:
    # Generate one 10-panel violin figure per workload
    output_dir.mkdir(parents=True, exist_ok=True)
    sns.set_theme(style="whitegrid")

    workloads = sorted(df_stats_with_miss_rates["workload"].unique())
    for workload_name in workloads:
        df_workload = df_stats_with_miss_rates[df_stats_with_miss_rates["workload"] == workload_name]
        # Create a 5x2 grid so we can plot all 10 metrics in one figure for this workload.
        figure, axes = plt.subplots(5, 2, figsize=(18, 22))
        axes_flat = axes.flatten()

        for metric_idx, metric_name in enumerate(PLOT_METRIC_ORDER):
            # Use the metric index to map each metric to its subplot panel.
            plot_axis = axes_flat[metric_idx]

            # Keep only rows for the metric being rendered in this panel.
            df_metric = df_workload[df_workload["metric"] == metric_name]

            # Draw the distribution shape per backend.
            # x: backend category (ebpf/perf), y: measured metric values across runs.
            # inner="quartile" shows quartile lines inside each violin.
            sns.violinplot(
                data=df_metric,
                x="profiler_family",
                y="value",
                order=BACKEND_ORDER,
                cut=0,
                inner="quartile",
                linewidth=1.0,
                ax=plot_axis,
            )

            # Set panel title/labels and keep a linear y-axis as requested.
            plot_axis.set_title(metric_name)
            plot_axis.set_xlabel("profiler_family")
            plot_axis.set_ylabel(METRIC_UNIT_BY_NAME[metric_name])
            plot_axis.set_yscale("linear")

            # Add median and relative-difference summary text in the panel corner.
            annotate_metric_medians(plot_axis, df_metric, metric_name)

        figure.suptitle(f"df_stats violin comparison for workload={workload_name}", fontsize=18)
        figure.tight_layout(rect=[0, 0, 1, 0.98])

        output_path = output_dir / f"{workload_name}_df_stats_violin.png"
        figure.savefig(output_path, dpi=160)
        plt.close(figure)


def main() -> None:
    # Compute results folder
    results_root = Path(__file__).resolve().parent.parent / "results"
    plots_output_dir = results_root / "plots"
    generated_plot_dirs = []

    # Load each ebpf/perf pair, then prepare and plot each pair independently
    for ebpf_folder_name, perf_folder_name in RESULT_FOLDER_PAIRS:
        print(f"[{ebpf_folder_name}]")
        df_stats_ebpf, _df_raw_metrics_ebpf = load_result_dataframes(results_root, ebpf_folder_name)
        print(f"[{perf_folder_name}]")
        df_stats_perf, _df_raw_metrics_perf = load_result_dataframes(results_root, perf_folder_name)
        df_stats_combined = pd.concat([df_stats_ebpf, df_stats_perf], ignore_index=True)

        # Prepare stats and derive miss-rate metrics
        df_stats_prepared = prepare_df_stats(df_stats_combined)
        df_stats_with_miss_rates = append_miss_rate_metrics(df_stats_prepared)

        pair_plot_dir = plots_output_dir / f"{ebpf_folder_name}__vs__{perf_folder_name}"
        generate_violin_plots(df_stats_with_miss_rates, pair_plot_dir)
        generated_plot_dirs.append(pair_plot_dir)

    print("Generated violin plots:")
    for pair_plot_dir in generated_plot_dirs:
        print(f"  - {pair_plot_dir}")


if __name__ == "__main__":
    main()
