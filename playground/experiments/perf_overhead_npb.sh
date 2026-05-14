#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLAYGROUND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PLAYGROUND_DIR/.." && pwd)"
cd "$REPO_ROOT"

MEASURE_SCRIPT="$PLAYGROUND_DIR/experiments/measure_perf_overhead.sh"

RUN_COUNT="${RUN_COUNT:-5}"
NPB_BIN_DIR="${NPB_BIN_DIR:-$PLAYGROUND_DIR/npb/bin}"
NPB_BENCHMARKS="${NPB_BENCHMARKS:-cg,mg,ft}"
NPB_CLASS="${NPB_CLASS:-B}"
NPB_EXEC_SUFFIX="${NPB_EXEC_SUFFIX:-x}"
NPB_MPI_PROCS="${NPB_MPI_PROCS:-0}"
NPB_MPI_LAUNCHER="${NPB_MPI_LAUNCHER:-mpirun}"
PROFILER_BACKEND="${PROFILER_BACKEND:-perf}"
RESULTS_ROOT="${RESULTS_ROOT:-$PLAYGROUND_DIR/results/${PROFILER_BACKEND}_overhead_npb}"

if [[ ! -d "$NPB_BIN_DIR" ]]; then
  echo "NPB bin directory not found: $NPB_BIN_DIR"
  echo "Set NPB_BIN_DIR to the folder containing binaries (e.g., cg.B.x, mg.B.x, ft.B.x)."
  exit 1
fi

mkdir -p "$RESULTS_ROOT"
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

  benchmark_output_dir="$RESULTS_ROOT/$benchmark"
  mkdir -p "$benchmark_output_dir"

  echo "=== Benchmark: $benchmark ==="
  if [[ "$NPB_MPI_PROCS" -gt 0 ]]; then
    OUTPUT_DIR="$benchmark_output_dir" RUN_COUNT="$RUN_COUNT" \
      "$MEASURE_SCRIPT" -- "$NPB_MPI_LAUNCHER" -np "$NPB_MPI_PROCS" "$benchmark_bin"
  else
    OUTPUT_DIR="$benchmark_output_dir" RUN_COUNT="$RUN_COUNT" \
      "$MEASURE_SCRIPT" -- "$benchmark_bin"
  fi
done

echo "Finished NPB wrapper runs."
echo "Results root: $RESULTS_ROOT"
