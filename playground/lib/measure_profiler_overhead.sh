#!/usr/bin/env bash

# Strict mode:
# -e: abort on unhandled command failure
# -u: abort on use of unset variable
# -o pipefail: propagate pipeline failure
set -euo pipefail

# ---------- Run configuration.

# Number of repetitions.
RUN_COUNT="${RUN_COUNT:-10}"

# Stats schema shared by both perf and eBPF backends.
# This script enforces this schema so outputs are directly comparable:
# - sample_idx
# - elapsed_ms
# - pid
# - l1_read_access_total
# - l1_read_miss_total
# - l2_read_access_total
# - l2_read_miss_total
# - llc_read_access_total
# - llc_read_miss_total
PERF_EVENT_LIST="L1-dcache-loads,L1-dcache-load-misses,l2_rqsts.references,l2_rqsts.miss,longest_lat_cache.reference,longest_lat_cache.miss"

# Delay between launching profiler attach and resuming target execution.
ATTACH_GRACE_SECONDS="${ATTACH_GRACE_SECONDS:-0.10}"

# Select profiler implementation used in profiled phase.
PROFILER_BACKEND="${PROFILER_BACKEND:-perf}"

# eBPF profiler executable path (only used when PROFILER_BACKEND=ebpf).
EBPF_PROFILER_BIN="${EBPF_PROFILER_BIN:-./build/cache_profiler}"

# eBPF sampler interval passed to cache_profiler.
EBPF_SAMPLE_INTERVAL_MS="${EBPF_SAMPLE_INTERVAL_MS:-200}"

# Default output directory is backend-aware so runs don't mix perf/ebpf by name.
OUTPUT_DIR="${OUTPUT_DIR:-$PWD/playground/results/${PROFILER_BACKEND}_resource_overhead_$(date +%Y%m%dT%H%M%S)}"

# Use sudo for profiler attachment when not root.
PERF_RUNNER=()
if [[ "${EUID}" -ne 0 ]]; then
  # The target runs as current user; only profiler attachment is elevated.
  PERF_RUNNER=("sudo")
fi

# Backend selection:
# - perf: uses `perf stat -p <pid>` with PERF_EVENT_LIST.
# - ebpf: uses cache_profiler attached to target PID.
if [[ "$PROFILER_BACKEND" != "perf" ]] && [[ "$PROFILER_BACKEND" != "ebpf" ]]; then
  echo "Invalid PROFILER_BACKEND: $PROFILER_BACKEND"
  exit 1
fi

if [[ "$PROFILER_BACKEND" == "perf" ]]; then
  # perf backend requires perf binary.
  if ! command -v perf >/dev/null 2>&1; then
    echo "perf not found in PATH."
    exit 1
  fi
fi

if [[ "$PROFILER_BACKEND" == "ebpf" ]]; then
  # eBPF backend requires built cache_profiler binary.
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
  # Optional separator for readability in call-sites.
  shift
fi
# Command and arguments to benchmark.
TARGET_CMD=("$@")

# Create root output directory once.
mkdir -p "$OUTPUT_DIR"

# Main process-level metrics CSV.
RAW_CSV="$OUTPUT_DIR/raw_process_metrics.csv"

# Profiler stats CSV
PROFILER_STATS_CSV="$OUTPUT_DIR/profiler_stats.csv"

# One row per measured process.
echo "run,scenario,process,wall_seconds,user_cpu_seconds,sys_cpu_seconds,maxrss_kb,exit_code" > "$RAW_CSV"
# One row per profiler-reported metric value.
echo "run,backend,metric,value,unit,source" > "$PROFILER_STATS_CSV"

# Shared cache-metric ordering used when writing profiler_stats.csv.
CANONICAL_CACHE_METRICS=(
  "l1_read_access_total"
  "l1_read_miss_total"
  "l2_read_access_total"
  "l2_read_miss_total"
  "llc_read_access_total"
  "llc_read_miss_total"
)

# Maps normalized perf event names to canonical metric names.
declare -A PERF_EVENT_TO_METRIC=(
  ["l1-dcache-loads"]="l1_read_access_total"
  ["l1-dcache-load-misses"]="l1_read_miss_total"
  ["l2_rqsts.references"]="l2_read_access_total"
  ["l2_rqsts.miss"]="l2_read_miss_total"
  ["longest_lat_cache.reference"]="llc_read_access_total"
  ["longest_lat_cache.miss"]="llc_read_miss_total"
)

# Normalizes perf event strings to stable dictionary keys.
normalizePerfEventName() {
  local raw_event_name="$1"
  local normalized_event_name
  normalized_event_name="${raw_event_name//[[:space:]]/}"
  normalized_event_name="${normalized_event_name,,}"
  normalized_event_name="${normalized_event_name#cpu/}"
  normalized_event_name="${normalized_event_name%/}"
  normalized_event_name="${normalized_event_name%%:*}"
  printf "%s" "$normalized_event_name"
}

for run_idx in $(seq 1 "$RUN_COUNT"); do
  # Each run gets its own directory with all raw logs/artifacts.
  run_dir="$OUTPUT_DIR/run_${run_idx}"
  mkdir -p "$run_dir"

  # Baseline target artifacts.
  baseline_time_file="$run_dir/target_baseline.time"
  baseline_stdout_file="$run_dir/target_baseline.stdout.log"
  baseline_stderr_file="$run_dir/target_baseline.stderr.log"

  # Profiled target artifacts.
  profiled_target_time_file="$run_dir/target_profiled.time"
  profiled_target_stdout_file="$run_dir/target_profiled.stdout.log"
  profiled_target_stderr_file="$run_dir/target_profiled.stderr.log"

  # Used to communicate the exact PID perf/eBPF should attach to.
  profiled_target_pid_file="$run_dir/target_profiled.pid"

  # Profiler process artifacts.
  profiler_time_file="$run_dir/profiler_attach.time"
  profiler_stdout_file="$run_dir/profiler_attach.stdout.log"
  profiler_stderr_file="$run_dir/profiler_attach.stderr.log"

  # eBPF profiler CSV (when using eBPF backend).
  ebpf_profiler_csv="$run_dir/ebpf_profiler_samples.csv"

  # 1) Baseline: run target normally and measure its resources.
  echo "=== Run $run_idx/$RUN_COUNT: baseline target ==="
  set +e
  # /usr/bin/time writes: wall;user;sys;maxrss.
  /usr/bin/time -f "%e;%U;%S;%M" -o "$baseline_time_file" "${TARGET_CMD[@]}" >"$baseline_stdout_file" 2>"$baseline_stderr_file"
  target_baseline_exit_code=$?
  set -e

  # Parse timing tuple from baseline time file.
  IFS=';' read -r baseline_wall_seconds baseline_user_seconds baseline_sys_seconds baseline_max_rss_kb < "$baseline_time_file"

  echo "$run_idx,baseline,target,$baseline_wall_seconds,$baseline_user_seconds,$baseline_sys_seconds,$baseline_max_rss_kb,$target_baseline_exit_code" >> "$RAW_CSV"

  # 2) Profiled run:
  #    - launch target and publish PID,
  #    - STOP target briefly,
  #    - attach selected profiler and measure profiler process,
  #    - CONT target and wait for completion.
  echo "=== Run $run_idx/$RUN_COUNT: profiled target + ${PROFILER_BACKEND} attach ==="

  rm -f "$profiled_target_pid_file"
  # Wrapper writes its own PID to file and then execs target command.
  # Because exec preserves PID, this gives us the exact PID to attach to.
  /usr/bin/time -f "%e;%U;%S;%M" -o "$profiled_target_time_file" \
    bash -c 'pid_file="$1"; shift; echo "$$" > "$pid_file"; exec "$@"' bash "$profiled_target_pid_file" "${TARGET_CMD[@]}" \
    >"$profiled_target_stdout_file" 2>"$profiled_target_stderr_file" &
  profiled_target_wrapper_pid=$!
  echo "Run $run_idx: launcher wrapper PID=$profiled_target_wrapper_pid"

  # Wait until target PID is published.
  while [[ ! -s "$profiled_target_pid_file" ]]; do
    sleep 0.01
  done
  target_profiled_pid="$(cat "$profiled_target_pid_file")"
  echo "Run $run_idx: target PID from pid file=$target_profiled_pid"

  # Pause target to reduce "work done before profiler attach" race.
  kill -STOP "$target_profiled_pid" >/dev/null 2>&1 || true

  if [[ "$PROFILER_BACKEND" == "perf" ]]; then
    # Attach perf stat to live target PID and time perf process resource use.
    /usr/bin/time -f "%e;%U;%S;%M" -o "$profiler_time_file" \
      "${PERF_RUNNER[@]}" perf stat --no-big-num -x ';' -e "$PERF_EVENT_LIST" -p "$target_profiled_pid" \
      1>"$profiler_stdout_file" 2>"$profiler_stderr_file" &
  elif [[ "$PROFILER_BACKEND" == "ebpf" ]]; then
    # Attach eBPF cache_profiler to live target PID and time profiler process.
    /usr/bin/time -f "%e;%U;%S;%M" -o "$profiler_time_file" \
      "${PERF_RUNNER[@]}" "$EBPF_PROFILER_BIN" \
      --csv-log \
      --csv-path "$run_dir" \
      --csv-filename "$(basename "$ebpf_profiler_csv")" \
      "$target_profiled_pid" "$EBPF_SAMPLE_INTERVAL_MS" \
      1>"$profiler_stdout_file" 2>"$profiler_stderr_file" &
  else
    echo "Unhandled PROFILER_BACKEND in attach path: $PROFILER_BACKEND"
    exit 1
  fi
  profiler_attach_pid=$!
  echo "Run $run_idx: profiler attach process PID=$profiler_attach_pid"

  # Give attach a short setup window before target resumes.
  sleep "$ATTACH_GRACE_SECONDS"
  kill -CONT "$target_profiled_pid" >/dev/null 2>&1 || true

  set +e
  # Wait for both target and profiler to finish and capture both exit codes.
  wait "$profiled_target_wrapper_pid"
  target_profiled_exit_code=$?
  wait "$profiler_attach_pid"
  profiler_attach_exit_code=$?
  set -e

  # Parse timed metrics for profiled target and profiler process.
  IFS=';' read -r profiled_wall_seconds profiled_user_seconds profiled_sys_seconds profiled_max_rss_kb < "$profiled_target_time_file"

  IFS=';' read -r profiler_wall_seconds profiler_user_seconds profiler_sys_seconds profiler_max_rss_kb < "$profiler_time_file"

  # Record process-level metrics rows.
  echo "$run_idx,profiled,target,$profiled_wall_seconds,$profiled_user_seconds,$profiled_sys_seconds,$profiled_max_rss_kb,$target_profiled_exit_code" >> "$RAW_CSV"
  if [[ "$PROFILER_BACKEND" == "perf" ]]; then
    echo "$run_idx,profiled,perf,$profiler_wall_seconds,$profiler_user_seconds,$profiler_sys_seconds,$profiler_max_rss_kb,$profiler_attach_exit_code" >> "$RAW_CSV"
    # Parse perf stat rows into canonical cache metrics and enforce completeness.
    declare -A perf_metric_values=()
    declare -A perf_metric_seen=()
    while IFS=';' read -r raw_count raw_unit raw_event _raw_runtime _raw_pct _raw_metric _raw_metric_unit; do
      if [[ -z "$raw_event" ]]; then
        # Skip blank/non-event lines.
        continue
      fi

      normalized_event_name="$(normalizePerfEventName "$raw_event")"
      if [[ -z "${PERF_EVENT_TO_METRIC[$normalized_event_name]+x}" ]]; then
        # Ignore non-cache summary/metric helper lines from perf output.
        continue
      fi

      metric_name="${PERF_EVENT_TO_METRIC[$normalized_event_name]}"

      # Normalize count formatting and reject unsupported/missing counters.
      normalized_count="$raw_count"
      if [[ -z "$normalized_count" ]] || [[ "$normalized_count" == "<not counted>" ]] || [[ "$normalized_count" == "<not supported>" ]]; then
        echo "perf failed to provide required event '$raw_event' in run $run_idx. Check PMU support and permissions."
        exit 1
      fi
      # Remove thousands separators.
      normalized_count="${normalized_count//,/}"

      perf_metric_values["$metric_name"]="$normalized_count"
      perf_metric_seen["$metric_name"]=1
    done < "$profiler_stderr_file"

    # perf stat is a single aggregate snapshot over the target's runtime.
    perf_sample_idx="1"
    perf_elapsed_ms="$(awk -v wall_seconds="$profiler_wall_seconds" 'BEGIN { printf "%.0f", wall_seconds * 1000 }')"
    echo "$run_idx,perf,sample_idx,$perf_sample_idx,index,$profiler_stderr_file" >> "$PROFILER_STATS_CSV"
    echo "$run_idx,perf,elapsed_ms,$perf_elapsed_ms,ms,$profiler_stderr_file" >> "$PROFILER_STATS_CSV"
    echo "$run_idx,perf,pid,$target_profiled_pid,pid,$profiler_stderr_file" >> "$PROFILER_STATS_CSV"

    for canonical_metric in "${CANONICAL_CACHE_METRICS[@]}"; do
      if [[ -z "${perf_metric_seen[$canonical_metric]+x}" ]]; then
        echo "perf output is missing required metric '$canonical_metric' in run $run_idx."
        echo "Expected perf events: $PERF_EVENT_LIST"
        exit 1
      fi
      echo "$run_idx,perf,$canonical_metric,${perf_metric_values[$canonical_metric]},count,$profiler_stderr_file" >> "$PROFILER_STATS_CSV"
    done
  elif [[ "$PROFILER_BACKEND" == "ebpf" ]]; then
    echo "$run_idx,profiled,ebpf_profiler,$profiler_wall_seconds,$profiler_user_seconds,$profiler_sys_seconds,$profiler_max_rss_kb,$profiler_attach_exit_code" >> "$RAW_CSV"

    # Keep the last cumulative sample from eBPF CSV as run-level profiler values.
    if [[ -f "$ebpf_profiler_csv" ]]; then
      # Drop CSV header and pick final sample row.
      last_ebpf_sample_row="$(tail -n +2 "$ebpf_profiler_csv" | tail -n 1)"
    else
      last_ebpf_sample_row=""
    fi
    if [[ -n "$last_ebpf_sample_row" ]]; then
      # Expand shared metadata + cache metrics for strict parity with perf
      # output schema.
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
    else
      echo "eBPF profiler did not produce a final sample row in run $run_idx."
      echo "Expected file with samples: $ebpf_profiler_csv"
      exit 1
    fi
  else
    echo "Unhandled PROFILER_BACKEND in process/stats recording path: $PROFILER_BACKEND"
    exit 1
  fi

done

# Final pointers printed for convenience.
echo "Completed $RUN_COUNT runs."
echo "Raw metrics CSV: $RAW_CSV"
echo "Profiler stats CSV: $PROFILER_STATS_CSV"
echo "Per-run logs under: $OUTPUT_DIR"
