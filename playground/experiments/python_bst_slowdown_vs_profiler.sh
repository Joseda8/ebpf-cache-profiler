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

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found in PATH."
  exit 1
fi

RUN_COUNT="${RUN_COUNT:-10}"
SAMPLE_INTERVAL_MS="${SAMPLE_INTERVAL_MS:-1000}"
WORKLOAD_SIZE="${WORKLOAD_SIZE:-9000000}"
RESULTS_DIR="$PLAYGROUND_DIR/results/python_bst_slowdown_vs_profiler"
COMPARISON_FILE="$RESULTS_DIR/slowdown_comparison.csv"
WORKLOAD_SCRIPT="$PLAYGROUND_DIR/workloads/python_bst_workload.py"

mkdir -p "$RESULTS_DIR"

echo "run,baseline_elapsed_seconds,perf_elapsed_seconds,profiler_elapsed_seconds,perf_vs_baseline_ratio,profiler_vs_baseline_ratio,profiler_vs_perf_slowdown_ratio" > "$COMPARISON_FILE"

for run_idx in $(seq 1 "$RUN_COUNT"); do
  BASELINE_LOG="$RESULTS_DIR/baseline_run_${run_idx}.log"
  PERF_LOG="$RESULTS_DIR/perf_stat_run_${run_idx}.log"
  PERF_WORKLOAD_LOG="$RESULTS_DIR/perf_workload_run_${run_idx}.log"
  PROFILER_CSV_FILE="profiler_samples_run_${run_idx}.csv"
  PROFILER_CSV_PATH="$RESULTS_DIR/$PROFILER_CSV_FILE"
  PROFILER_WORKLOAD_LOG="$RESULTS_DIR/profiler_workload_run_${run_idx}.log"

  echo "=== Run $run_idx/$RUN_COUNT: baseline phase ==="
  BASELINE_START_MS="$(current_time_ms)"
  python3 "$WORKLOAD_SCRIPT" "$WORKLOAD_SIZE" > "$BASELINE_LOG" 2>&1 &
  BASELINE_PID=$!
  set +e
  wait "$BASELINE_PID"
  set -e
  BASELINE_END_MS="$(current_time_ms)"
  BASELINE_ELAPSED_MS="$((BASELINE_END_MS - BASELINE_START_MS))"
  BASELINE_ELAPSED_SECONDS="$(awk -v value_ms="$BASELINE_ELAPSED_MS" 'BEGIN { printf "%.3f", value_ms / 1000.0 }')"

  echo "=== Run $run_idx/$RUN_COUNT: perf phase ==="
  PERF_START_MS="$(current_time_ms)"
  python3 "$WORKLOAD_SCRIPT" "$WORKLOAD_SIZE" > "$PERF_WORKLOAD_LOG" 2>&1 &
  PERF_PID=$!
  run_perf_stat_for_pid_until_exit "$PERF_PID" "$PERF_LOG"
  set +e
  wait "$PERF_PID"
  set -e
  PERF_END_MS="$(current_time_ms)"
  PERF_ELAPSED_MS="$((PERF_END_MS - PERF_START_MS))"
  PERF_ELAPSED_SECONDS="$(awk -v value_ms="$PERF_ELAPSED_MS" 'BEGIN { printf "%.3f", value_ms / 1000.0 }')"

  echo "=== Run $run_idx/$RUN_COUNT: profiler phase ==="
  PROFILER_START_MS="$(current_time_ms)"
  python3 "$WORKLOAD_SCRIPT" "$WORKLOAD_SIZE" > "$PROFILER_WORKLOAD_LOG" 2>&1 &
  PROFILER_PID=$!
  rm -f "$PROFILER_CSV_PATH"
  run_profiler_for_pid_until_exit "$PROFILER_PID" "$SAMPLE_INTERVAL_MS" "$RESULTS_DIR" "$PROFILER_CSV_FILE" 0
  set +e
  wait "$PROFILER_PID"
  set -e
  PROFILER_END_MS="$(current_time_ms)"
  PROFILER_ELAPSED_MS="$((PROFILER_END_MS - PROFILER_START_MS))"
  PROFILER_ELAPSED_SECONDS="$(awk -v value_ms="$PROFILER_ELAPSED_MS" 'BEGIN { printf "%.3f", value_ms / 1000.0 }')"

  PERF_VS_BASELINE_RATIO="$(awk -v measured="$PERF_ELAPSED_MS" -v baseline="$BASELINE_ELAPSED_MS" 'BEGIN { if (baseline <= 0) { printf "0.000" } else { printf "%.3f", measured / baseline } }')"
  PROFILER_VS_BASELINE_RATIO="$(awk -v measured="$PROFILER_ELAPSED_MS" -v baseline="$BASELINE_ELAPSED_MS" 'BEGIN { if (baseline <= 0) { printf "0.000" } else { printf "%.3f", measured / baseline } }')"
  PROFILER_VS_PERF_SLOWDOWN_RATIO="$(awk -v profiler_ratio="$PROFILER_VS_BASELINE_RATIO" -v perf_ratio="$PERF_VS_BASELINE_RATIO" 'BEGIN { if (perf_ratio <= 0) { printf "0.000" } else { printf "%.3f", profiler_ratio / perf_ratio } }')"

  echo "$run_idx,$BASELINE_ELAPSED_SECONDS,$PERF_ELAPSED_SECONDS,$PROFILER_ELAPSED_SECONDS,$PERF_VS_BASELINE_RATIO,$PROFILER_VS_BASELINE_RATIO,$PROFILER_VS_PERF_SLOWDOWN_RATIO" >> "$COMPARISON_FILE"
done

echo "Finished $RUN_COUNT runs."
echo "Results saved in $RESULTS_DIR"
echo "Comparison file: $COMPARISON_FILE"
