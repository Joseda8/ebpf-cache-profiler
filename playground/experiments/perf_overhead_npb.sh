#!/usr/bin/env bash

# Strict mode for predictable wrapper behavior.
set -euo pipefail

# Shared root-relative path constants.
source ./playground/lib/playground_paths.sh

# Wrapper configuration.
RUN_COUNT="${RUN_COUNT:-5}"
# Directory containing compiled NPB executables.
NPB_BIN_DIR="${NPB_BIN_DIR:-$PLAYGROUND_ROOT/npb/bin}"
# Comma-separated benchmark names (without class/suffix).
NPB_BENCHMARKS="${NPB_BENCHMARKS:-cg,mg,ft}"
# NPB class and executable suffix.
NPB_CLASS="${NPB_CLASS:-B}"
NPB_EXEC_SUFFIX="${NPB_EXEC_SUFFIX:-x}"
# Optional MPI launch configuration (0 => direct execution without MPI launcher).
NPB_MPI_PROCS="${NPB_MPI_PROCS:-0}"
NPB_MPI_LAUNCHER="${NPB_MPI_LAUNCHER:-mpirun}"
# Backend + output root.
PROFILER_BACKEND="${PROFILER_BACKEND:-all}"
RESULTS_ROOT="${RESULTS_ROOT:-$PLAYGROUND_RESULTS_DIR/${PROFILER_BACKEND}_overhead_npb}"

# Validate benchmark directory.
if [[ ! -d "$NPB_BIN_DIR" ]]; then
  echo "NPB bin directory not found: $NPB_BIN_DIR"
  echo "Set NPB_BIN_DIR to the folder containing binaries (e.g., cg.B.x, mg.B.x, ft.B.x)."
  exit 1
fi

# Prepare output root and split benchmark list.
mkdir -p "$RESULTS_ROOT"
IFS=',' read -r -a BENCHMARK_LIST <<< "$NPB_BENCHMARKS"

# Iterate each requested NPB benchmark.
for raw_benchmark in "${BENCHMARK_LIST[@]}"; do
  # Remove accidental spaces from CSV input.
  benchmark="${raw_benchmark//[[:space:]]/}"
  if [[ -z "$benchmark" ]]; then
    # Skip empty entries, e.g. trailing commas.
    continue
  fi

  # Build full executable path like cg.B.x.
  benchmark_bin="$NPB_BIN_DIR/${benchmark}.${NPB_CLASS}.${NPB_EXEC_SUFFIX}"
  if [[ ! -x "$benchmark_bin" ]]; then
    echo "NPB benchmark binary not found or not executable: $benchmark_bin"
    exit 1
  fi

  # Keep each benchmark in its own output subdirectory.
  benchmark_output_dir="$RESULTS_ROOT/$benchmark"
  mkdir -p "$benchmark_output_dir"

  echo "=== Benchmark: $benchmark ==="
  if [[ "$NPB_MPI_PROCS" -gt 0 ]]; then
    # MPI path: measure launcher + benchmark process under core runner.
    OUTPUT_DIR="$benchmark_output_dir" RUN_COUNT="$RUN_COUNT" \
      "$MEASURE_SCRIPT" -- "$NPB_MPI_LAUNCHER" -np "$NPB_MPI_PROCS" "$benchmark_bin"
  else
    # Non-MPI path: execute benchmark binary directly.
    OUTPUT_DIR="$benchmark_output_dir" RUN_COUNT="$RUN_COUNT" \
      "$MEASURE_SCRIPT" -- "$benchmark_bin"
  fi
done

# Final status lines for operator feedback.
echo "Finished NPB wrapper runs."
echo "Results root: $RESULTS_ROOT"
