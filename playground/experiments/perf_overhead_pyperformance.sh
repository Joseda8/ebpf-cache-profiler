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

if ! "${SUDO_RUNNER[@]}" python3 -m pyperformance --help >/dev/null 2>&1; then
  echo "pyperformance is not installed for python3."
  echo "Install with: python3 -m pip install pyperformance"
  echo "If using sudo internally, install it in the sudo-resolved python environment too."
  exit 1
fi

RUN_COUNT="${RUN_COUNT:-5}"
PYPERFORMANCE_BENCHMARKS="${PYPERFORMANCE_BENCHMARKS:-json_dumps,pickle_list,regex_v8,nbody,python_startup}"
PYPERFORMANCE_RUN_MODE="${PYPERFORMANCE_RUN_MODE:-fast}"
RESULTS_DIR="$PLAYGROUND_DIR/results/perf_overhead_pyperformance"
COMPARISON_FILE="$RESULTS_DIR/perf_overhead.csv"

mkdir -p "$RESULTS_DIR"

echo "suite,benchmark,run,mode,elapsed_seconds,ratio_vs_baseline" > "$COMPARISON_FILE"

RUN_MODE_ARGS=()
case "$PYPERFORMANCE_RUN_MODE" in
  fast)
    RUN_MODE_ARGS=(--fast)
    ;;
  rigorous)
    RUN_MODE_ARGS=(--rigorous)
    ;;
  default)
    RUN_MODE_ARGS=()
    ;;
  *)
    echo "Invalid PYPERFORMANCE_RUN_MODE: $PYPERFORMANCE_RUN_MODE"
    echo "Allowed values: fast, rigorous, default"
    exit 1
    ;;
esac

IFS=',' read -r -a BENCHMARK_LIST <<< "$PYPERFORMANCE_BENCHMARKS"

for raw_benchmark in "${BENCHMARK_LIST[@]}"; do
  benchmark="${raw_benchmark//[[:space:]]/}"
  if [[ -z "$benchmark" ]]; then
    continue
  fi

  echo "=== Benchmark: $benchmark ==="

  for run_idx in $(seq 1 "$RUN_COUNT"); do
    BASELINE_LOG="$RESULTS_DIR/${benchmark}_baseline_run_${run_idx}.log"
    BASELINE_JSON="$RESULTS_DIR/${benchmark}_baseline_run_${run_idx}.json"
    PERF_LOG="$RESULTS_DIR/${benchmark}_perf_run_${run_idx}.log"
    PERF_JSON="$RESULTS_DIR/${benchmark}_perf_run_${run_idx}.json"
    PERF_STAT_LOG="$RESULTS_DIR/${benchmark}_perf_stat_run_${run_idx}.log"

    echo "  Run $run_idx/$RUN_COUNT: baseline"
    BASELINE_START_MS="$(current_time_ms)"
    "${SUDO_RUNNER[@]}" python3 -m pyperformance run --benchmarks "$benchmark" --output "$BASELINE_JSON" "${RUN_MODE_ARGS[@]}" > "$BASELINE_LOG" 2>&1
    BASELINE_END_MS="$(current_time_ms)"
    BASELINE_ELAPSED_MS="$((BASELINE_END_MS - BASELINE_START_MS))"
    BASELINE_ELAPSED_SECONDS="$(format_seconds_from_ms "$BASELINE_ELAPSED_MS")"

    echo "  Run $run_idx/$RUN_COUNT: perf"
    PERF_START_MS="$(current_time_ms)"
    run_perf_stat_for_command "$PERF_STAT_LOG" python3 -m pyperformance run --benchmarks "$benchmark" --output "$PERF_JSON" "${RUN_MODE_ARGS[@]}" > "$PERF_LOG" 2>&1
    PERF_END_MS="$(current_time_ms)"
    PERF_ELAPSED_MS="$((PERF_END_MS - PERF_START_MS))"
    PERF_ELAPSED_SECONDS="$(format_seconds_from_ms "$PERF_ELAPSED_MS")"

    PERF_RATIO="$(awk -v perf_ms="$PERF_ELAPSED_MS" -v baseline_ms="$BASELINE_ELAPSED_MS" 'BEGIN { if (baseline_ms <= 0) { printf "0.000" } else { printf "%.3f", perf_ms / baseline_ms } }')"

    echo "pyperformance,$benchmark,$run_idx,baseline,$BASELINE_ELAPSED_SECONDS,1.000" >> "$COMPARISON_FILE"
    echo "pyperformance,$benchmark,$run_idx,perf,$PERF_ELAPSED_SECONDS,$PERF_RATIO" >> "$COMPARISON_FILE"
  done
done

echo "Finished suite: pyperformance"
echo "Results saved in $RESULTS_DIR"
echo "Comparison file: $COMPARISON_FILE"
