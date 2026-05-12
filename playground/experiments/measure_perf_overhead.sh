#!/usr/bin/env bash

set -euo pipefail

# Run configuration.
RUN_COUNT="${RUN_COUNT:-10}"
EVENTS="${EVENTS:-cycles,instructions,cache-references,cache-misses,context-switches,cpu-migrations}"
ATTACH_GRACE_SECONDS="${ATTACH_GRACE_SECONDS:-0.10}"
OUTPUT_DIR="${OUTPUT_DIR:-$PWD/playground/results/perf_resource_overhead_$(date +%Y%m%dT%H%M%S)}"

# Event intent:
# - cycles/instructions: total compute work and IPC/CPI behavior under profiling.
# - cache-references/cache-misses: memory hierarchy pressure under profiling.
# - context-switches/cpu-migrations: scheduler-induced perturbation.

# Use sudo for perf when not root.
PERF_RUNNER=()
if [[ "${EUID}" -ne 0 ]]; then
  PERF_RUNNER=("sudo")
fi

# Target command must be provided after optional "--".
if [[ "$#" -eq 0 ]]; then
  echo "Usage: $0 -- <target_command> [args ...]"
  exit 1
fi
if [[ "$1" == "--" ]]; then
  shift
fi
TARGET_CMD=("$@")

mkdir -p "$OUTPUT_DIR"
RAW_CSV="$OUTPUT_DIR/raw_process_metrics.csv"

# One row per measured process.
echo "run,scenario,process,wall_seconds,user_cpu_seconds,sys_cpu_seconds,maxrss_kb,exit_code" > "$RAW_CSV"

for run_idx in $(seq 1 "$RUN_COUNT"); do
  run_dir="$OUTPUT_DIR/run_${run_idx}"
  mkdir -p "$run_dir"

  baseline_time_file="$run_dir/target_baseline.time"
  baseline_stdout_file="$run_dir/target_baseline.stdout.log"
  baseline_stderr_file="$run_dir/target_baseline.stderr.log"

  profiled_target_time_file="$run_dir/target_profiled.time"
  profiled_target_stdout_file="$run_dir/target_profiled.stdout.log"
  profiled_target_stderr_file="$run_dir/target_profiled.stderr.log"
  profiled_target_pid_file="$run_dir/target_profiled.pid"

  perf_time_file="$run_dir/perf_attach.time"
  perf_stdout_file="$run_dir/perf_attach.stdout.log"
  perf_stat_file="$run_dir/perf_stat_output.log"

  # 1) Baseline: run target normally and measure its resources.
  echo "=== Run $run_idx/$RUN_COUNT: baseline target ==="
  set +e
  /usr/bin/time -f "%e;%U;%S;%M" -o "$baseline_time_file" "${TARGET_CMD[@]}" >"$baseline_stdout_file" 2>"$baseline_stderr_file"
  target_baseline_exit_code=$?
  set -e

  IFS=';' read -r baseline_wall_seconds baseline_user_seconds baseline_sys_seconds baseline_max_rss_kb < "$baseline_time_file"

  echo "$run_idx,baseline,target,$baseline_wall_seconds,$baseline_user_seconds,$baseline_sys_seconds,$baseline_max_rss_kb,$target_baseline_exit_code" >> "$RAW_CSV"

  # 2) Profiled run:
  #    - launch target and publish PID,
  #    - STOP target briefly,
  #    - attach perf and measure perf process,
  #    - CONT target and wait for completion.
  echo "=== Run $run_idx/$RUN_COUNT: profiled target + perf attach ==="

  rm -f "$profiled_target_pid_file"
  /usr/bin/time -f "%e;%U;%S;%M" -o "$profiled_target_time_file" \
    bash -c 'pid_file="$1"; shift; echo "$$" > "$pid_file"; exec "$@"' bash "$profiled_target_pid_file" "${TARGET_CMD[@]}" \
    >"$profiled_target_stdout_file" 2>"$profiled_target_stderr_file" &
  profiled_target_wrapper_pid=$!

  while [[ ! -s "$profiled_target_pid_file" ]]; do
    sleep 0.01
  done
  target_profiled_pid="$(cat "$profiled_target_pid_file")"

  kill -STOP "$target_profiled_pid" >/dev/null 2>&1 || true

  /usr/bin/time -f "%e;%U;%S;%M" -o "$perf_time_file" \
    "${PERF_RUNNER[@]}" perf stat --no-big-num -x ';' -e "$EVENTS" -p "$target_profiled_pid" \
    1>"$perf_stdout_file" 2>"$perf_stat_file" &
  perf_attach_pid=$!

  sleep "$ATTACH_GRACE_SECONDS"
  kill -CONT "$target_profiled_pid" >/dev/null 2>&1 || true

  set +e
  wait "$profiled_target_wrapper_pid"
  target_profiled_exit_code=$?
  wait "$perf_attach_pid"
  perf_attach_exit_code=$?
  set -e

  IFS=';' read -r profiled_wall_seconds profiled_user_seconds profiled_sys_seconds profiled_max_rss_kb < "$profiled_target_time_file"

  IFS=';' read -r perf_wall_seconds perf_user_seconds perf_sys_seconds perf_max_rss_kb < "$perf_time_file"

  echo "$run_idx,profiled,target,$profiled_wall_seconds,$profiled_user_seconds,$profiled_sys_seconds,$profiled_max_rss_kb,$target_profiled_exit_code" >> "$RAW_CSV"
  echo "$run_idx,profiled,perf,$perf_wall_seconds,$perf_user_seconds,$perf_sys_seconds,$perf_max_rss_kb,$perf_attach_exit_code" >> "$RAW_CSV"

done

echo "Completed $RUN_COUNT runs."
echo "Raw metrics CSV: $RAW_CSV"
echo "Per-run logs under: $OUTPUT_DIR"
