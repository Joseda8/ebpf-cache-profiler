#!/usr/bin/env bash

# Root-relative playground path constants.
# These are intentionally simple and assume scripts run from repo root.
PLAYGROUND_ROOT="./playground"
PLAYGROUND_LIB_DIR="$PLAYGROUND_ROOT/lib"
PLAYGROUND_EXPERIMENTS_DIR="$PLAYGROUND_ROOT/experiments"
PLAYGROUND_RESULTS_DIR="$PLAYGROUND_ROOT/results"
PLAYGROUND_WORKLOADS_DIR="$PLAYGROUND_ROOT/workloads"
PLAYGROUND_BIN_DIR="$PLAYGROUND_ROOT/bin"

# Shared core measurement runner.
MEASURE_SCRIPT="$PLAYGROUND_LIB_DIR/measure_profiler_overhead.sh"
