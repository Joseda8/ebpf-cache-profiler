#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLAYGROUND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PLAYGROUND_DIR/.." && pwd)"
cd "$REPO_ROOT"

MEASURE_SCRIPT="$PLAYGROUND_DIR/experiments/measure_perf_overhead.sh"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found in PATH."
  exit 1
fi

if ! python3 -m pyperformance --help >/dev/null 2>&1; then
  echo "pyperformance is not installed for python3."
  echo "Install with: python3 -m pip install pyperformance"
  exit 1
fi

RUN_COUNT="${RUN_COUNT:-5}"
PYPERFORMANCE_BENCHMARKS="${PYPERFORMANCE_BENCHMARKS:-json_dumps,pickle_list,regex_v8,nbody,python_startup}"
PYPERFORMANCE_RUN_MODE="${PYPERFORMANCE_RUN_MODE:-fast}"
PROFILER_BACKEND="${PROFILER_BACKEND:-perf}"
RESULTS_ROOT="${RESULTS_ROOT:-$PLAYGROUND_DIR/results/${PROFILER_BACKEND}_overhead_pyperformance}"

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

mkdir -p "$RESULTS_ROOT"
IFS=',' read -r -a BENCHMARK_LIST <<< "$PYPERFORMANCE_BENCHMARKS"

for raw_benchmark in "${BENCHMARK_LIST[@]}"; do
  benchmark="${raw_benchmark//[[:space:]]/}"
  if [[ -z "$benchmark" ]]; then
    continue
  fi

  benchmark_output_dir="$RESULTS_ROOT/$benchmark"
  mkdir -p "$benchmark_output_dir"

  echo "=== Benchmark: $benchmark ==="
  OUTPUT_DIR="$benchmark_output_dir" RUN_COUNT="$RUN_COUNT" \
    "$MEASURE_SCRIPT" -- python3 -m pyperformance run --benchmarks "$benchmark" "${RUN_MODE_ARGS[@]}"
done

echo "Finished pyperformance wrapper runs."
echo "Results root: $RESULTS_ROOT"
