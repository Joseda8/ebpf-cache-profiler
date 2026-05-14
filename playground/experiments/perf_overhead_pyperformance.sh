#!/usr/bin/env bash

# Strict mode for predictable wrapper behavior.
set -euo pipefail

# Resolve script/repo paths once so wrapper is location-independent.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLAYGROUND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PLAYGROUND_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Core measurement script this wrapper delegates to.
MEASURE_SCRIPT="$PLAYGROUND_DIR/lib/measure_perf_overhead.sh"

# Tool checks required by pyperformance runs.
if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found in PATH."
  exit 1
fi

if ! python3 -m pyperformance --help >/dev/null 2>&1; then
  echo "pyperformance is not installed for python3."
  echo "Install with: python3 -m pip install pyperformance"
  exit 1
fi

# Wrapper configuration.
RUN_COUNT="${RUN_COUNT:-5}"
# Comma-separated pyperformance benchmark names.
PYPERFORMANCE_BENCHMARKS="${PYPERFORMANCE_BENCHMARKS:-json_dumps,pickle_list,regex_v8,nbody,python_startup}"
# Runtime mode forwarded to pyperformance.
PYPERFORMANCE_RUN_MODE="${PYPERFORMANCE_RUN_MODE:-fast}"
# Backend + output root.
PROFILER_BACKEND="${PROFILER_BACKEND:-perf}"
RESULTS_ROOT="${RESULTS_ROOT:-$PLAYGROUND_DIR/results/${PROFILER_BACKEND}_overhead_pyperformance}"

# Build argument list for selected pyperformance mode.
RUN_MODE_ARGS=()
case "$PYPERFORMANCE_RUN_MODE" in
  fast)
    RUN_MODE_ARGS=(--fast)
    ;;
  rigorous)
    RUN_MODE_ARGS=(--rigorous)
    ;;
  default)
    # Use pyperformance defaults (no explicit mode flag).
    RUN_MODE_ARGS=()
    ;;
  *)
    echo "Invalid PYPERFORMANCE_RUN_MODE: $PYPERFORMANCE_RUN_MODE"
    echo "Allowed values: fast, rigorous, default"
    exit 1
    ;;
esac

# Prepare output root and split benchmark list.
mkdir -p "$RESULTS_ROOT"
IFS=',' read -r -a BENCHMARK_LIST <<< "$PYPERFORMANCE_BENCHMARKS"

# Iterate each requested pyperformance benchmark.
for raw_benchmark in "${BENCHMARK_LIST[@]}"; do
  # Remove accidental spaces from CSV input.
  benchmark="${raw_benchmark//[[:space:]]/}"
  if [[ -z "$benchmark" ]]; then
    # Skip empty entries, e.g. trailing commas.
    continue
  fi

  # Keep each benchmark in its own output subdirectory.
  benchmark_output_dir="$RESULTS_ROOT/$benchmark"
  mkdir -p "$benchmark_output_dir"

  echo "=== Benchmark: $benchmark ==="
  # Delegate baseline/profiled resource measurement to core runner.
  OUTPUT_DIR="$benchmark_output_dir" RUN_COUNT="$RUN_COUNT" \
    "$MEASURE_SCRIPT" -- python3 -m pyperformance run --benchmarks "$benchmark" "${RUN_MODE_ARGS[@]}"
done

# Final status lines for operator feedback.
echo "Finished pyperformance wrapper runs."
echo "Results root: $RESULTS_ROOT"
