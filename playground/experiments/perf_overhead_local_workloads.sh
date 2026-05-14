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

if ! command -v g++ >/dev/null 2>&1; then
  echo "g++ not found in PATH."
  exit 1
fi

RUN_COUNT="${RUN_COUNT:-5}"
PROFILER_BACKEND="${PROFILER_BACKEND:-perf}"
RESULTS_ROOT="${RESULTS_ROOT:-$PLAYGROUND_DIR/results/${PROFILER_BACKEND}_overhead_local_workloads}"

PYTHON_NODE_COUNT="${PYTHON_NODE_COUNT:-5000000}"
PYTHON_WORKLOAD="$PLAYGROUND_DIR/workloads/python_random_bst_workload.py"

THREADED_SLEEP_SECONDS="${THREADED_SLEEP_SECONDS:-1}"
THREADED_COMPUTE_SECONDS="${THREADED_COMPUTE_SECONDS:-20}"
THREADED_THREAD_COUNT="${THREADED_THREAD_COUNT:-8}"
THREADED_WORKLOAD_SRC="$PLAYGROUND_DIR/workloads/threaded_memory_workload.cpp"
THREADED_WORKLOAD_BIN="$PLAYGROUND_DIR/bin/threaded_memory_workload"

mkdir -p "$RESULTS_ROOT"
mkdir -p "$PLAYGROUND_DIR/bin"

if [[ ! -x "$THREADED_WORKLOAD_BIN" ]] || [[ "$THREADED_WORKLOAD_SRC" -nt "$THREADED_WORKLOAD_BIN" ]]; then
  echo "Compiling threaded workload..."
  g++ -O2 -std=c++17 -pthread "$THREADED_WORKLOAD_SRC" -o "$THREADED_WORKLOAD_BIN"
fi

echo "=== Local workload: python_random_bst ==="
OUTPUT_DIR="$RESULTS_ROOT/python_random_bst" RUN_COUNT="$RUN_COUNT" \
  "$MEASURE_SCRIPT" -- python3 "$PYTHON_WORKLOAD" "$PYTHON_NODE_COUNT"

echo "=== Local workload: threaded_memory ==="
OUTPUT_DIR="$RESULTS_ROOT/threaded_memory" RUN_COUNT="$RUN_COUNT" \
  "$MEASURE_SCRIPT" -- "$THREADED_WORKLOAD_BIN" "$THREADED_SLEEP_SECONDS" "$THREADED_COMPUTE_SECONDS" "$THREADED_THREAD_COUNT"

echo "Finished local-workload wrapper runs."
echo "Results root: $RESULTS_ROOT"
