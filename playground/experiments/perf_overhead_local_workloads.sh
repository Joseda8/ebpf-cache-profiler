#!/usr/bin/env bash

# Strict mode for predictable wrapper behavior.
set -euo pipefail

# Shared root-relative path constants.
source ./playground/lib/playground_paths.sh

# Tool checks for wrapped workloads.
if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found in PATH."
  exit 1
fi

if ! command -v g++ >/dev/null 2>&1; then
  echo "g++ not found in PATH."
  exit 1
fi

# Shared run knobs for both workloads.
RUN_COUNT="${RUN_COUNT:-5}"
PROFILER_BACKEND="${PROFILER_BACKEND:-perf}"
# Backend-aware default output root.
RESULTS_ROOT="${RESULTS_ROOT:-$PLAYGROUND_RESULTS_DIR/${PROFILER_BACKEND}_overhead_local_workloads}"

# Python workload configuration.
PYTHON_NODE_COUNT="${PYTHON_NODE_COUNT:-5000000}"
PYTHON_WORKLOAD="$PLAYGROUND_WORKLOADS_DIR/python_random_bst_workload.py"

# Threaded workload configuration.
THREADED_SLEEP_SECONDS="${THREADED_SLEEP_SECONDS:-1}"
THREADED_COMPUTE_SECONDS="${THREADED_COMPUTE_SECONDS:-20}"
THREADED_THREAD_COUNT="${THREADED_THREAD_COUNT:-8}"
THREADED_WORKLOAD_SRC="$PLAYGROUND_WORKLOADS_DIR/threaded_memory_workload.cpp"
THREADED_WORKLOAD_BIN="$PLAYGROUND_BIN_DIR/threaded_memory_workload"

# Ensure output + binary directories exist.
mkdir -p "$RESULTS_ROOT"
mkdir -p "$PLAYGROUND_BIN_DIR"

# Build threaded workload when missing or stale.
if [[ ! -x "$THREADED_WORKLOAD_BIN" ]] || [[ "$THREADED_WORKLOAD_SRC" -nt "$THREADED_WORKLOAD_BIN" ]]; then
  echo "Compiling threaded workload..."
  g++ -O2 -std=c++17 -pthread "$THREADED_WORKLOAD_SRC" -o "$THREADED_WORKLOAD_BIN"
fi

# Run measurement for Python random-BST workload.
echo "=== Local workload: python_random_bst ==="
OUTPUT_DIR="$RESULTS_ROOT/python_random_bst" RUN_COUNT="$RUN_COUNT" \
  "$MEASURE_SCRIPT" -- python3 "$PYTHON_WORKLOAD" "$PYTHON_NODE_COUNT"

# Run measurement for threaded C++ workload.
echo "=== Local workload: threaded_memory ==="
OUTPUT_DIR="$RESULTS_ROOT/threaded_memory" RUN_COUNT="$RUN_COUNT" \
  "$MEASURE_SCRIPT" -- "$THREADED_WORKLOAD_BIN" "$THREADED_SLEEP_SECONDS" "$THREADED_COMPUTE_SECONDS" "$THREADED_THREAD_COUNT"

# Final status lines for operator feedback.
echo "Finished local-workload wrapper runs."
echo "Results root: $RESULTS_ROOT"
