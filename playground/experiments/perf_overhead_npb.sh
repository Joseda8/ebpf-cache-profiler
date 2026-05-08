#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLAYGROUND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PLAYGROUND_DIR/.." && pwd)"
cd "$REPO_ROOT"

source "$PLAYGROUND_DIR/lib/experiment_common.sh"

if ! command -v perf >/dev/null 2>&1; then
  echo "perf not found in PATH."
  exit 1
fi

RUN_COUNT="${RUN_COUNT:-5}"
NPB_BIN_DIR="${NPB_BIN_DIR:-$PLAYGROUND_DIR/npb/bin}"
NPB_BENCHMARKS="${NPB_BENCHMARKS:-cg,mg,ft}"
NPB_CLASS="${NPB_CLASS:-B}"
NPB_EXEC_SUFFIX="${NPB_EXEC_SUFFIX:-x}"
NPB_MPI_PROCS="${NPB_MPI_PROCS:-0}"
NPB_MPI_LAUNCHER="${NPB_MPI_LAUNCHER:-mpirun}"
RESULTS_DIR="$PLAYGROUND_DIR/results/perf_overhead_npb"
COMPARISON_FILE="$RESULTS_DIR/perf_overhead.csv"

mkdir -p "$RESULTS_DIR"

echo "suite,benchmark,run,mode,elapsed_seconds,ratio_vs_baseline" > "$COMPARISON_FILE"

if [[ ! -d "$NPB_BIN_DIR" ]]; then
  echo "NPB bin directory not found: $NPB_BIN_DIR"
  echo "Set NPB_BIN_DIR to the folder containing binaries (e.g., cg.B.x, mg.B.x, ft.B.x)."
  exit 1
fi

IFS=',' read -r -a BENCHMARK_LIST <<< "$NPB_BENCHMARKS"

for raw_benchmark in "${BENCHMARK_LIST[@]}"; do
  benchmark="${raw_benchmark//[[:space:]]/}"
  if [[ -z "$benchmark" ]]; then
    continue
  fi

  benchmark_bin="$NPB_BIN_DIR/${benchmark}.${NPB_CLASS}.${NPB_EXEC_SUFFIX}"
  if [[ ! -x "$benchmark_bin" ]]; then
    echo "NPB benchmark binary not found or not executable: $benchmark_bin"
    exit 1
  fi

  echo "=== Benchmark: $benchmark ==="

  for run_idx in $(seq 1 "$RUN_COUNT"); do
    BASELINE_LOG="$RESULTS_DIR/${benchmark}_baseline_run_${run_idx}.log"
    PERF_LOG="$RESULTS_DIR/${benchmark}_perf_run_${run_idx}.log"
    PERF_STAT_LOG="$RESULTS_DIR/${benchmark}_perf_stat_run_${run_idx}.log"

    RUN_CMD=("$benchmark_bin")
    if [[ "$NPB_MPI_PROCS" -gt 0 ]]; then
      RUN_CMD=("$NPB_MPI_LAUNCHER" -np "$NPB_MPI_PROCS" "$benchmark_bin")
    fi

    echo "  Run $run_idx/$RUN_COUNT: baseline"
    BASELINE_START_MS="$(current_time_ms)"
    "${SUDO_RUNNER[@]}" "${RUN_CMD[@]}" > "$BASELINE_LOG" 2>&1
    BASELINE_END_MS="$(current_time_ms)"
    BASELINE_ELAPSED_MS="$((BASELINE_END_MS - BASELINE_START_MS))"
    BASELINE_ELAPSED_SECONDS="$(format_seconds_from_ms "$BASELINE_ELAPSED_MS")"

    echo "  Run $run_idx/$RUN_COUNT: perf"
    PERF_START_MS="$(current_time_ms)"
    run_perf_stat_for_command "$PERF_STAT_LOG" "${RUN_CMD[@]}" > "$PERF_LOG" 2>&1
    PERF_END_MS="$(current_time_ms)"
    PERF_ELAPSED_MS="$((PERF_END_MS - PERF_START_MS))"
    PERF_ELAPSED_SECONDS="$(format_seconds_from_ms "$PERF_ELAPSED_MS")"

    PERF_RATIO="$(awk -v perf_ms="$PERF_ELAPSED_MS" -v baseline_ms="$BASELINE_ELAPSED_MS" 'BEGIN { if (baseline_ms <= 0) { printf "0.000" } else { printf "%.3f", perf_ms / baseline_ms } }')"

    echo "npb,$benchmark,$run_idx,baseline,$BASELINE_ELAPSED_SECONDS,1.000" >> "$COMPARISON_FILE"
    echo "npb,$benchmark,$run_idx,perf,$PERF_ELAPSED_SECONDS,$PERF_RATIO" >> "$COMPARISON_FILE"
  done
done

echo "Finished suite: npb"
echo "Results saved in $RESULTS_DIR"
echo "Comparison file: $COMPARISON_FILE"
