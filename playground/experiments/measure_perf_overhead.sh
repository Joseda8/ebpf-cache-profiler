#!/usr/bin/env bash

set -euo pipefail

# Run configuration.
RUN_COUNT="${RUN_COUNT:-10}"
EVENTS="${EVENTS:-cycles,instructions,cache-references,cache-misses,context-switches,cpu-migrations}"
ATTACH_GRACE_SECONDS="${ATTACH_GRACE_SECONDS:-0.10}"
PROFILER_BACKEND="${PROFILER_BACKEND:-perf}"
EBPF_PROFILER_BIN="${EBPF_PROFILER_BIN:-./build/cache_profiler}"
EBPF_SAMPLE_INTERVAL_MS="${EBPF_SAMPLE_INTERVAL_MS:-200}"
OUTPUT_DIR="${OUTPUT_DIR:-$PWD/playground/results/${PROFILER_BACKEND}_resource_overhead_$(date +%Y%m%dT%H%M%S)}"

# Event intent:
# - cycles/instructions: total compute work and IPC/CPI behavior under profiling.
# - cache-references/cache-misses: memory hierarchy pressure under profiling.
# - context-switches/cpu-migrations: scheduler-induced perturbation.

# Use sudo for profiler attachment when not root.
PERF_RUNNER=()
if [[ "${EUID}" -ne 0 ]]; then
  PERF_RUNNER=("sudo")
fi

# Backend selection:
# - perf: uses `perf stat -p <pid>` with EVENTS list.
# - ebpf: uses cache_profiler attached to target PID.
if [[ "$PROFILER_BACKEND" != "perf" ]] && [[ "$PROFILER_BACKEND" != "ebpf" ]]; then
  echo "Invalid PROFILER_BACKEND: $PROFILER_BACKEND"
  echo "Allowed values: perf, ebpf"
  exit 1
fi

if [[ "$PROFILER_BACKEND" == "perf" ]]; then
  if ! command -v perf >/dev/null 2>&1; then
    echo "perf not found in PATH."
    exit 1
  fi
fi

if [[ "$PROFILER_BACKEND" == "ebpf" ]]; then
  if [[ ! -x "$EBPF_PROFILER_BIN" ]]; then
    echo "eBPF profiler binary not found or not executable: $EBPF_PROFILER_BIN"
    exit 1
  fi
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
PROFILER_STATS_CSV="$OUTPUT_DIR/profiler_stats.csv"

# One row per measured process.
echo "run,scenario,process,wall_seconds,user_cpu_seconds,sys_cpu_seconds,maxrss_kb,exit_code" > "$RAW_CSV"
# One row per profiler-reported metric value.
echo "run,backend,metric,value,unit,source" > "$PROFILER_STATS_CSV"

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

  profiler_time_file="$run_dir/profiler_attach.time"
  profiler_stdout_file="$run_dir/profiler_attach.stdout.log"
  profiler_stderr_file="$run_dir/profiler_attach.stderr.log"
  ebpf_profiler_csv="$run_dir/ebpf_profiler_samples.csv"

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
  #    - attach selected profiler and measure profiler process,
  #    - CONT target and wait for completion.
  echo "=== Run $run_idx/$RUN_COUNT: profiled target + ${PROFILER_BACKEND} attach ==="

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

  if [[ "$PROFILER_BACKEND" == "perf" ]]; then
    /usr/bin/time -f "%e;%U;%S;%M" -o "$profiler_time_file" \
      "${PERF_RUNNER[@]}" perf stat --no-big-num -x ';' -e "$EVENTS" -p "$target_profiled_pid" \
      1>"$profiler_stdout_file" 2>"$profiler_stderr_file" &
  else
    /usr/bin/time -f "%e;%U;%S;%M" -o "$profiler_time_file" \
      "${PERF_RUNNER[@]}" "$EBPF_PROFILER_BIN" \
      --csv-log \
      --csv-path "$run_dir" \
      --csv-filename "$(basename "$ebpf_profiler_csv")" \
      "$target_profiled_pid" "$EBPF_SAMPLE_INTERVAL_MS" \
      1>"$profiler_stdout_file" 2>"$profiler_stderr_file" &
  fi
  profiler_attach_pid=$!

  sleep "$ATTACH_GRACE_SECONDS"
  kill -CONT "$target_profiled_pid" >/dev/null 2>&1 || true

  set +e
  wait "$profiled_target_wrapper_pid"
  target_profiled_exit_code=$?
  wait "$profiler_attach_pid"
  profiler_attach_exit_code=$?
  set -e

  IFS=';' read -r profiled_wall_seconds profiled_user_seconds profiled_sys_seconds profiled_max_rss_kb < "$profiled_target_time_file"

  IFS=';' read -r profiler_wall_seconds profiler_user_seconds profiler_sys_seconds profiler_max_rss_kb < "$profiler_time_file"

  echo "$run_idx,profiled,target,$profiled_wall_seconds,$profiled_user_seconds,$profiled_sys_seconds,$profiled_max_rss_kb,$target_profiled_exit_code" >> "$RAW_CSV"
  if [[ "$PROFILER_BACKEND" == "perf" ]]; then
    echo "$run_idx,profiled,perf,$profiler_wall_seconds,$profiler_user_seconds,$profiler_sys_seconds,$profiler_max_rss_kb,$profiler_attach_exit_code" >> "$RAW_CSV"
    while IFS=';' read -r raw_count raw_unit raw_event _raw_runtime _raw_pct _raw_metric _raw_metric_unit; do
      if [[ -z "$raw_event" ]]; then
        continue
      fi

      normalized_count="$raw_count"
      if [[ -z "$normalized_count" ]] || [[ "$normalized_count" == "<not counted>" ]]; then
        normalized_count="0"
      else
        normalized_count="${normalized_count//,/}"
      fi

      echo "$run_idx,perf,$raw_event,$normalized_count,${raw_unit:-count},$profiler_stderr_file" >> "$PROFILER_STATS_CSV"
    done < "$profiler_stderr_file"
  else
    echo "$run_idx,profiled,ebpf_profiler,$profiler_wall_seconds,$profiler_user_seconds,$profiler_sys_seconds,$profiler_max_rss_kb,$profiler_attach_exit_code" >> "$RAW_CSV"

    # Keep the last cumulative sample from eBPF CSV as run-level profiler values.
    if [[ -f "$ebpf_profiler_csv" ]]; then
      last_ebpf_sample_row="$(tail -n +2 "$ebpf_profiler_csv" | tail -n 1)"
    else
      last_ebpf_sample_row=""
    fi
    if [[ -n "$last_ebpf_sample_row" ]]; then
      IFS=',' read -r sample_idx elapsed_ms profiled_pid l1_read_access_total l1_read_miss_total l2_read_access_total l2_read_miss_total llc_read_access_total llc_read_miss_total <<< "$last_ebpf_sample_row"
      echo "$run_idx,ebpf,sample_idx,$sample_idx,index,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,elapsed_ms,$elapsed_ms,ms,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,pid,$profiled_pid,pid,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,l1_read_access_total,$l1_read_access_total,count,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,l1_read_miss_total,$l1_read_miss_total,count,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,l2_read_access_total,$l2_read_access_total,count,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,l2_read_miss_total,$l2_read_miss_total,count,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,llc_read_access_total,$llc_read_access_total,count,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
      echo "$run_idx,ebpf,llc_read_miss_total,$llc_read_miss_total,count,$ebpf_profiler_csv" >> "$PROFILER_STATS_CSV"
    fi
  fi

done

echo "Completed $RUN_COUNT runs."
echo "Raw metrics CSV: $RAW_CSV"
echo "Profiler stats CSV: $PROFILER_STATS_CSV"
echo "Per-run logs under: $OUTPUT_DIR"
