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

RUN_COUNT=10
SLEEP_SECONDS=3
COMPUTE_SECONDS=7
MEASURE_SECONDS=$((SLEEP_SECONDS + COMPUTE_SECONDS))
MEASURE_MS="$((MEASURE_SECONDS * 1000))"
SAMPLE_INTERVAL_MS=200
RESULTS_DIR="$PLAYGROUND_DIR/results/python_vs_profiler"
COMPARISON_FILE="$RESULTS_DIR/miss_rate_comparison.csv"
WORKLOAD_SCRIPT="$PLAYGROUND_DIR/workloads/python_memory_workload.py"

mkdir -p "$RESULTS_DIR"
write_comparison_header "$COMPARISON_FILE"

for run_idx in $(seq 1 "$RUN_COUNT"); do
  PERF_LOG="$RESULTS_DIR/perf_stat_run_${run_idx}.log"
  PROFILER_CSV_FILE="profiler_samples_run_${run_idx}.csv"
  PROFILER_CSV_PATH="$RESULTS_DIR/$PROFILER_CSV_FILE"

  echo "=== Run $run_idx/$RUN_COUNT: perf phase (${MEASURE_SECONDS}s) ==="
  python3 "$WORKLOAD_SCRIPT" "$SLEEP_SECONDS" "$COMPUTE_SECONDS" &
  WORKLOAD_PID=$!
  PERF_PID="$WORKLOAD_PID"
  echo "Started perf workload PID=$WORKLOAD_PID"

  run_perf_stat_for_pid "$WORKLOAD_PID" "$MEASURE_MS" "$PERF_LOG"
  wait "$WORKLOAD_PID" || true

  PERF_RATES="$(compute_perf_miss_rates "$PERF_LOG")"
  IFS=',' read -r PERF_L1_MISS_RATE PERF_L2_MISS_RATE PERF_LLC_MISS_RATE <<< "$PERF_RATES"

  echo "=== Run $run_idx/$RUN_COUNT: profiler phase (${MEASURE_SECONDS}s) ==="
  python3 "$WORKLOAD_SCRIPT" "$SLEEP_SECONDS" "$COMPUTE_SECONDS" &
  WORKLOAD_PID=$!
  echo "Started profiler workload PID=$WORKLOAD_PID"

  rm -f "$PROFILER_CSV_PATH"
  run_profiler_for_pid "$WORKLOAD_PID" "$SAMPLE_INTERVAL_MS" "$MEASURE_MS" "$RESULTS_DIR" "$PROFILER_CSV_FILE" 0
  wait "$WORKLOAD_PID" || true

  PROFILER_RATES="$(compute_profiler_miss_rates "$PROFILER_CSV_PATH")"
  IFS=',' read -r PROFILED_PID PROFILER_L1_MISS_RATE PROFILER_L2_MISS_RATE PROFILER_LLC_MISS_RATE <<< "$PROFILER_RATES"

  echo "$run_idx,$PERF_PID,$PROFILED_PID,$PERF_L1_MISS_RATE,$PROFILER_L1_MISS_RATE,$PERF_L2_MISS_RATE,$PROFILER_L2_MISS_RATE,$PERF_LLC_MISS_RATE,$PROFILER_LLC_MISS_RATE" >> "$COMPARISON_FILE"
done

echo "Finished $RUN_COUNT runs."
echo "Results saved in $RESULTS_DIR"
echo "Comparison file: $COMPARISON_FILE"
