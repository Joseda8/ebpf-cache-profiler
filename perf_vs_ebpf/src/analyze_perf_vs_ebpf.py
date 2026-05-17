#!/usr/bin/env python3

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns


RESULT_BATCH_FOLDERS = [
    "all_overhead_local_workloads",
    "all_overhead_pyperformance",
    "all_overhead_npb",
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
RAW_RESOURCE_METRICS = ["wall_seconds", "user_cpu_seconds", "sys_cpu_seconds", "maxrss_kb"]
RAW_RESOURCE_UNITS = {
    "wall_seconds": "seconds",
    "user_cpu_seconds": "seconds",
    "sys_cpu_seconds": "seconds",
    "maxrss_kb": "kB",
}
RAW_EXPORT_METRIC_PREFIX = {
    "wall_seconds": "wall",
    "user_cpu_seconds": "user",
    "sys_cpu_seconds": "sys",
    "maxrss_kb": "rss_kb",
}
RAW_BACKEND_COLORS = {"ebpf": "#1f77b4", "perf": "#ff7f0e"}
CAPTURE_QUALITY_SUBDIR = "capture_quality"
OVERHEAD_IMPACT_SUBDIR = "overhead_impact"


def format_metric_value(metric_name: str, value: float) -> str:
    # Format annotation values by metric scale
    if metric_name.endswith("_pct"):
        return f"{value:.3f}"
    if metric_name.endswith("_miss_rate"):
        return f"{value:.6f}"
    if metric_name == "elapsed_s":
        return f"{value:.2f}"
    if metric_name.endswith("_seconds"):
        return f"{value:.3f}"
    return f"{value:.3e}"


def annotate_metric_medians(plot_axis: plt.Axes, df_metric: pd.DataFrame, metric_name: str) -> None:
    # Compute medians and add a summary annotation in axis corner
    # Median is the 50th percentile of run values for each backend.
    median_by_backend = df_metric.groupby("profiler_family", observed=True)["value"].median()
    median_ebpf = float(median_by_backend["ebpf"])
    median_perf = float(median_by_backend["perf"])
    # Ratio compares backend magnitudes directly (ebpf relative to perf).
    ratio_ebpf_perf = median_ebpf / median_perf
    # Relative change percentage versus perf baseline.
    percent_diff = ((median_ebpf - median_perf) / median_perf) * 100.0

    annotation_text = (
        f"median_ebpf: {format_metric_value(metric_name, median_ebpf)}\n"
        f"median_perf: {format_metric_value(metric_name, median_perf)}\n"
        f"ratio(ebpf/perf): {ratio_ebpf_perf:.6f}\n"
        f"%diff vs perf: {percent_diff:+.3f}%"
    )
    if metric_name.endswith("_miss_rate"):
        # Percentage-point difference: absolute gap between two rates.
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

    # Derive workload and profiler family from source_file and backend column.
    source_parts = df_stats_prepared["source_file"].str.split("/", expand=True)
    df_stats_prepared["result_folder_name"] = source_parts[0]
    df_stats_prepared["workload"] = source_parts[1]
    df_stats_prepared["profiler_family"] = df_stats_prepared["backend"].astype(str)

    return df_stats_prepared


def append_miss_rate_metrics(df_stats_prepared: pd.DataFrame) -> pd.DataFrame:
    # Pivot raw counters by metric
    pivot_index_columns = ["source_file", "run", "backend", "workload", "profiler_family"]
    df_pivot = df_stats_prepared.pivot(index=pivot_index_columns, columns="metric", values="value")

    for miss_rate_name, metric_names in MISS_RATE_METRICS.items():
        miss_metric_name, access_metric_name = metric_names
        # Miss rate = misses / accesses.
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


def prepare_df_raw_metrics(df_raw_metrics_combined: pd.DataFrame) -> pd.DataFrame:
    # Cast numeric fields and derive workload/profiler family labels.
    df_raw_prepared = df_raw_metrics_combined.copy()
    df_raw_prepared["run"] = pd.to_numeric(df_raw_prepared["run"], errors="raise").astype(int)
    df_raw_prepared["wall_seconds"] = pd.to_numeric(df_raw_prepared["wall_seconds"], errors="raise").astype(float)
    df_raw_prepared["user_cpu_seconds"] = pd.to_numeric(df_raw_prepared["user_cpu_seconds"], errors="raise").astype(float)
    df_raw_prepared["sys_cpu_seconds"] = pd.to_numeric(df_raw_prepared["sys_cpu_seconds"], errors="raise").astype(float)
    df_raw_prepared["maxrss_kb"] = pd.to_numeric(df_raw_prepared["maxrss_kb"], errors="raise").astype(float)

    source_parts = df_raw_prepared["source_file"].str.split("/", expand=True)
    df_raw_prepared["result_folder_name"] = source_parts[0]
    df_raw_prepared["workload"] = source_parts[1]

    # Map profiled scenarios to backend family.
    scenario_to_backend = {
        "profiled_ebpf": "ebpf",
        "profiled_perf": "perf",
    }
    df_raw_prepared["profiler_family"] = df_raw_prepared["scenario"].map(scenario_to_backend)
    return df_raw_prepared


def build_target_impact_dataset(df_raw_prepared: pd.DataFrame) -> pd.DataFrame:
    # Pair one baseline target row with each profiled target row by workload/run.
    join_keys = ["workload", "run"]
    df_target = df_raw_prepared[df_raw_prepared["process"] == "target"].copy()

    df_baseline = df_target[df_target["scenario"] == "baseline"][join_keys + RAW_RESOURCE_METRICS].rename(
        columns={metric_name: f"{metric_name}_baseline" for metric_name in RAW_RESOURCE_METRICS}
    )
    df_profiled = df_target[df_target["scenario"].isin(["profiled_ebpf", "profiled_perf"])][join_keys + ["profiler_family"] + RAW_RESOURCE_METRICS].rename(
        columns={metric_name: f"{metric_name}_profiled" for metric_name in RAW_RESOURCE_METRICS}
    )
    df_target_joined = df_profiled.merge(df_baseline, on=join_keys)

    for metric_name in RAW_RESOURCE_METRICS:
        baseline_col_name = f"{metric_name}_baseline"
        profiled_col_name = f"{metric_name}_profiled"
        overhead_abs_col_name = f"{metric_name}_overhead_abs"
        overhead_pct_col_name = f"{metric_name}_overhead_pct"
        # Absolute change from baseline.
        overhead_abs = df_target_joined[profiled_col_name] - df_target_joined[baseline_col_name]
        df_target_joined[overhead_abs_col_name] = overhead_abs
        # Relative change as ((profiled - baseline) / baseline) * 100, representing overhead percentage versus baseline.
        df_target_joined[overhead_pct_col_name] = (overhead_abs / df_target_joined[baseline_col_name]) * 100.0
    return df_target_joined


def build_profiler_cost_dataset(df_raw_prepared: pd.DataFrame) -> pd.DataFrame:
    # Keep profiled rows for profiler processes only (ebpf_profiler vs perf).
    profiler_mask = (
        ((df_raw_prepared["scenario"] == "profiled_ebpf") & (df_raw_prepared["process"] == "ebpf_profiler"))
        | ((df_raw_prepared["scenario"] == "profiled_perf") & (df_raw_prepared["process"] == "perf"))
    )
    return df_raw_prepared[profiler_mask].copy()


def compute_ratio_and_diff_text(median_ebpf: float, median_perf: float) -> tuple[str, str]:
    # Format backend comparison ratio/%diff text.
    if median_perf == 0.0:
        return "inf", "inf"
    # Ratio = ebpf / perf and %diff = ((ebpf - perf) / perf) * 100.
    ratio_ebpf_perf = median_ebpf / median_perf
    percent_diff = ((median_ebpf - median_perf) / median_perf) * 100.0
    return f"{ratio_ebpf_perf:.6f}", f"{percent_diff:+.3f}%"


def add_raw_annotation(plot_axis: plt.Axes, metric_name: str, median_ebpf: float, median_perf: float) -> None:
    # Place median/ratio summary in panel corner.
    ratio_text, percent_diff_text = compute_ratio_and_diff_text(median_ebpf, median_perf)
    annotation_text = (
        f"median_ebpf: {format_metric_value(metric_name, median_ebpf)}\n"
        f"median_perf: {format_metric_value(metric_name, median_perf)}\n"
        f"ratio(ebpf/perf): {ratio_text}\n"
        f"%diff vs perf: {percent_diff_text}"
    )
    plot_axis.text(
        0.02,
        0.98,
        annotation_text,
        transform=plot_axis.transAxes,
        ha="left",
        va="top",
        fontsize=8,
        bbox={"boxstyle": "round,pad=0.3", "facecolor": "white", "alpha": 0.85, "edgecolor": "gray"},
    )


def plot_target_absolute_dumbbell(plot_axis: plt.Axes, df_target_workload: pd.DataFrame, metric_name: str) -> None:
    # Draw baseline->profiled medians as a paired dumbbell per backend.
    median_pairs = {}
    for backend_name in BACKEND_ORDER:
        df_backend = df_target_workload[df_target_workload["profiler_family"] == backend_name]
        # Median baseline/profiled values across runs for this backend.
        baseline_median = float(df_backend[f"{metric_name}_baseline"].median())
        profiled_median = float(df_backend[f"{metric_name}_profiled"].median())
        median_pairs[backend_name] = (baseline_median, profiled_median)

    for backend_idx, backend_name in enumerate(BACKEND_ORDER):
        baseline_median, profiled_median = median_pairs[backend_name]
        color = RAW_BACKEND_COLORS[backend_name]
        plot_axis.plot([baseline_median, profiled_median], [backend_idx, backend_idx], color=color, linewidth=2.0)
        plot_axis.scatter([baseline_median], [backend_idx], marker="o", s=55, color="gray", label=None, zorder=3)
        plot_axis.scatter([profiled_median], [backend_idx], marker="s", s=55, color=color, label=None, zorder=3)

    plot_axis.set_yticks(range(len(BACKEND_ORDER)))
    plot_axis.set_yticklabels(BACKEND_ORDER)
    plot_axis.set_xlabel(RAW_RESOURCE_UNITS[metric_name])
    plot_axis.set_title(f"{metric_name}: baseline -> profiled")

    ratio_text, percent_diff_text = compute_ratio_and_diff_text(
        median_pairs["ebpf"][1],
        median_pairs["perf"][1],
    )
    annotation_text = (
        f"ebpf baseline={format_metric_value(metric_name, median_pairs['ebpf'][0])}, "
        + f"profiled={format_metric_value(metric_name, median_pairs['ebpf'][1])}\n"
        + f"perf baseline={format_metric_value(metric_name, median_pairs['perf'][0])}, "
        + f"profiled={format_metric_value(metric_name, median_pairs['perf'][1])}\n"
        + f"profiled ratio(ebpf/perf)={ratio_text}, %diff={percent_diff_text}"
    )
    plot_axis.text(
        0.5,
        0.5,
        annotation_text,
        transform=plot_axis.transAxes,
        ha="center",
        va="center",
        fontsize=8,
        bbox={"boxstyle": "round,pad=0.3", "facecolor": "white", "alpha": 0.85, "edgecolor": "gray"},
    )


def plot_profiler_cost_bar_with_dots(plot_axis: plt.Axes, df_profiler_workload: pd.DataFrame, metric_name: str) -> None:
    # Draw median bar + run dots for profiler self-cost.
    df_metric = df_profiler_workload[["profiler_family", metric_name]].copy()
    df_metric = df_metric.rename(columns={metric_name: "value"})

    df_medians = (
        # Bar height is median resource usage across runs for each backend.
        df_metric.groupby("profiler_family", observed=True)["value"]
        .median()
        .reindex(BACKEND_ORDER)
        .reset_index()
    )
    sns.barplot(
        data=df_medians,
        x="profiler_family",
        y="value",
        order=BACKEND_ORDER,
        color="lightgray",
        ax=plot_axis,
    )
    for patch_idx, bar_patch in enumerate(plot_axis.patches):
        backend_name = BACKEND_ORDER[patch_idx]
        bar_patch.set_facecolor(RAW_BACKEND_COLORS[backend_name])

    plot_axis.set_xlabel("profiler_family")
    plot_axis.set_ylabel(RAW_RESOURCE_UNITS[metric_name])
    plot_axis.set_title(f"profiler cost: {metric_name}")

    median_by_backend = df_medians.set_index("profiler_family")["value"]
    add_raw_annotation(plot_axis, metric_name, float(median_by_backend["ebpf"]), float(median_by_backend["perf"]))


def generate_raw_overview_plots(df_target_impact: pd.DataFrame, df_profiler_cost: pd.DataFrame, plots_root: Path, pair_name: str) -> None:
    # Generate one raw overview figure per workload (target impact + profiler self-cost).
    workloads = sorted(df_target_impact["workload"].unique())
    for workload_name in workloads:
        # Overhead figures belong in the overhead-focused output subtree.
        workload_output_dir = plots_root / workload_name / OVERHEAD_IMPACT_SUBDIR
        workload_output_dir.mkdir(parents=True, exist_ok=True)

        df_target_workload = df_target_impact[df_target_impact["workload"] == workload_name]
        df_profiler_workload = df_profiler_cost[df_profiler_cost["workload"] == workload_name]

        figure, axes = plt.subplots(2, 4, figsize=(26, 10))
        for metric_idx, metric_name in enumerate(RAW_RESOURCE_METRICS):
            plot_target_absolute_dumbbell(axes[0, metric_idx], df_target_workload, metric_name)
            plot_profiler_cost_bar_with_dots(axes[1, metric_idx], df_profiler_workload, metric_name)

        figure.suptitle(f"raw_process_metrics overview ({pair_name}, workload={workload_name})", fontsize=17)
        figure.tight_layout(rect=[0, 0, 1, 0.97])
        output_path = workload_output_dir / f"{pair_name}__raw_overview.svg"
        figure.savefig(output_path, format="svg")
        plt.close(figure)


def export_plot_dataframes(
    plots_root: Path,
    pair_name: str,
    df_stats_with_miss_rates: pd.DataFrame,
    df_target_impact: pd.DataFrame,
    df_profiler_cost: pd.DataFrame,
) -> None:
    # Export the final DataFrames that feed the plotting functions.
    # These files are the tabular version of the data shown in the plots.
    workloads = sorted(df_stats_with_miss_rates["workload"].unique())
    for workload_name in workloads:
        # Split CSV exports by analysis goal: capture quality vs overhead impact.
        capture_output_dir = plots_root / workload_name / CAPTURE_QUALITY_SUBDIR
        overhead_output_dir = plots_root / workload_name / OVERHEAD_IMPACT_SUBDIR
        capture_output_dir.mkdir(parents=True, exist_ok=True)
        overhead_output_dir.mkdir(parents=True, exist_ok=True)

        df_stats_export = df_stats_with_miss_rates[df_stats_with_miss_rates["workload"] == workload_name].copy()
        # Keep metric/backend category order aligned with panel order in violin figures.
        df_stats_export["metric"] = pd.Categorical(df_stats_export["metric"], categories=PLOT_METRIC_ORDER, ordered=True)
        df_stats_export["profiler_family"] = pd.Categorical(df_stats_export["profiler_family"], categories=BACKEND_ORDER, ordered=True)
        stats_sort_columns = ["metric", "profiler_family", "run"]
        df_stats_export = df_stats_export.sort_values(stats_sort_columns).reset_index(drop=True)
        # Keep only columns needed to read values exactly as plotted.
        stats_column_order = [
            "workload",
            "metric",
            "profiler_family",
            "run",
            "value",
            "unit",
            "backend",
        ]
        existing_stats_columns = [column_name for column_name in stats_column_order if column_name in df_stats_export.columns]
        df_stats_export = df_stats_export[existing_stats_columns]
        df_stats_export.to_csv(capture_output_dir / f"{pair_name}__df_stats_with_miss_rates.csv", index=False)

        df_target_export = df_target_impact[df_target_impact["workload"] == workload_name].copy()
        # Keep backend order stable so rows read as ebpf then perf for each run.
        df_target_export["profiler_family"] = pd.Categorical(df_target_export["profiler_family"], categories=BACKEND_ORDER, ordered=True)
        target_sort_columns = ["profiler_family", "run"]
        df_target_export = df_target_export.sort_values(target_sort_columns).reset_index(drop=True)
        # Shorten wide-table column names for easier side-by-side reading with plot labels.
        target_rename_columns = {}
        for metric_name in RAW_RESOURCE_METRICS:
            metric_prefix = RAW_EXPORT_METRIC_PREFIX[metric_name]
            target_rename_columns[f"{metric_name}_baseline"] = f"{metric_prefix}_baseline"
            target_rename_columns[f"{metric_name}_profiled"] = f"{metric_prefix}_profiled"
            target_rename_columns[f"{metric_name}_overhead_abs"] = f"{metric_prefix}_ovhd_abs"
            target_rename_columns[f"{metric_name}_overhead_pct"] = f"{metric_prefix}_ovhd_pct"
        df_target_export = df_target_export.rename(columns=target_rename_columns)

        # Order baseline->profiled->overhead columns per metric to match dumbbell interpretation flow.
        target_column_order = ["workload", "profiler_family", "run"]
        for metric_name in RAW_RESOURCE_METRICS:
            metric_prefix = RAW_EXPORT_METRIC_PREFIX[metric_name]
            target_column_order.append(f"{metric_prefix}_baseline")
            target_column_order.append(f"{metric_prefix}_profiled")
            target_column_order.append(f"{metric_prefix}_ovhd_abs")
            target_column_order.append(f"{metric_prefix}_ovhd_pct")
        df_target_export = df_target_export[target_column_order]
        df_target_export.to_csv(overhead_output_dir / f"{pair_name}__df_target_impact.csv", index=False)

        df_profiler_export = df_profiler_cost[df_profiler_cost["workload"] == workload_name].copy()
        # Keep backend order stable to compare ebpf/perf medians and runs quickly.
        df_profiler_export["profiler_family"] = pd.Categorical(df_profiler_export["profiler_family"], categories=BACKEND_ORDER, ordered=True)
        profiler_sort_columns = ["profiler_family", "run"]
        df_profiler_export = df_profiler_export.sort_values(profiler_sort_columns).reset_index(drop=True)

        # Shorten profiler resource column names in the exported CSV.
        profiler_rename_columns = {}
        for metric_name in RAW_RESOURCE_METRICS:
            metric_prefix = RAW_EXPORT_METRIC_PREFIX[metric_name]
            profiler_rename_columns[metric_name] = metric_prefix
        df_profiler_export = df_profiler_export.rename(columns=profiler_rename_columns)

        profiler_column_order = [
            "workload",
            "profiler_family",
            "run",
            "scenario",
            "process",
            "wall",
            "user",
            "sys",
            "rss_kb",
        ]
        existing_profiler_columns = [column_name for column_name in profiler_column_order if column_name in df_profiler_export.columns]
        df_profiler_export = df_profiler_export[existing_profiler_columns]
        df_profiler_export.to_csv(overhead_output_dir / f"{pair_name}__df_profiler_cost.csv", index=False)


def generate_violin_plots(df_stats_with_miss_rates: pd.DataFrame, plots_root: Path, pair_name: str) -> None:
    # Generate one 10-panel violin figure per workload
    sns.set_theme(style="whitegrid")

    workloads = sorted(df_stats_with_miss_rates["workload"].unique())
    for workload_name in workloads:
        # Capture-quality figures belong in the capture-focused output subtree.
        workload_output_dir = plots_root / workload_name / CAPTURE_QUALITY_SUBDIR
        workload_output_dir.mkdir(parents=True, exist_ok=True)

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

        output_path = workload_output_dir / f"{pair_name}__df_stats_violin.svg"
        figure.savefig(output_path, format="svg")
        plt.close(figure)


def main() -> None:
    # ----------- Compute results folder
    results_root = Path(__file__).resolve().parent.parent / "results"
    raw_data_root = results_root / "raw_data"
    plots_output_dir = results_root / "plots"

    # Load each all-overhead batch and process it independently.
    for result_batch_name in RESULT_BATCH_FOLDERS:

        # ----------- Load raw data
        print(f"[{result_batch_name}]")
        df_stats_combined, df_raw_metrics_combined = load_result_dataframes(raw_data_root, result_batch_name)

        # Prepare stats and derive miss-rate metrics
        df_stats_prepared = prepare_df_stats(df_stats_combined)
        df_stats_with_miss_rates = append_miss_rate_metrics(df_stats_prepared)

        generate_violin_plots(df_stats_with_miss_rates, plots_output_dir, result_batch_name)

        # Prepare raw metrics and generate one overview figure per workload.
        df_raw_prepared = prepare_df_raw_metrics(df_raw_metrics_combined)
        df_target_impact = build_target_impact_dataset(df_raw_prepared)
        df_profiler_cost = build_profiler_cost_dataset(df_raw_prepared)
        generate_raw_overview_plots(df_target_impact, df_profiler_cost, plots_output_dir, result_batch_name)

        export_plot_dataframes(plots_output_dir, result_batch_name, df_stats_with_miss_rates, df_target_impact, df_profiler_cost)


if __name__ == "__main__":
    main()
    print("Data visualization flow finished")
