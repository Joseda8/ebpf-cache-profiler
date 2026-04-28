#!/usr/bin/env bash

set -euo pipefail

PERF_EVENTS=(
  "L1-dcache-loads:u"
  "L1-dcache-load-misses:u"
  "l2_rqsts.references:u"
  "l2_rqsts.miss:u"
  "longest_lat_cache.reference:u"
  "longest_lat_cache.miss:u"
)

format_percent() {
  local numerator="$1"
  local denominator="$2"
  awk -v n="$numerator" -v d="$denominator" 'BEGIN { if (d <= 0) { printf "0.00" } else { printf "%.2f", (100.0*n)/d } }'
}

normalize_count() {
  local raw_value="$1"
  if [[ -z "$raw_value" ]] || [[ "$raw_value" == "<not counted>" ]]; then
    echo "0"
    return
  fi
  echo "${raw_value//,/}"
}

extract_perf_count() {
  local perf_log="$1"
  local event_name="$2"
  local raw_value
  raw_value="$(awk -F';' -v ev="$event_name" '$3==ev { print $1; exit }' "$perf_log")"
  normalize_count "$raw_value"
}

run_perf_stat_for_pid() {
  local pid="$1"
  local measure_ms="$2"
  local perf_log="$3"

  sudo perf stat --no-big-num -x ';' -p "$pid" --timeout "$measure_ms" \
    -e "${PERF_EVENTS[0]}" \
    -e "${PERF_EVENTS[1]}" \
    -e "${PERF_EVENTS[2]}" \
    -e "${PERF_EVENTS[3]}" \
    -e "${PERF_EVENTS[4]}" \
    -e "${PERF_EVENTS[5]}" \
    2> "$perf_log"
}

compute_perf_miss_rates() {
  local perf_log="$1"
  local perf_l1_access
  local perf_l1_miss
  local perf_l2_access
  local perf_l2_miss
  local perf_llc_access
  local perf_llc_miss

  perf_l1_access="$(extract_perf_count "$perf_log" "L1-dcache-loads")"
  perf_l1_miss="$(extract_perf_count "$perf_log" "L1-dcache-load-misses")"
  perf_l2_access="$(extract_perf_count "$perf_log" "l2_rqsts.references:u")"
  perf_l2_miss="$(extract_perf_count "$perf_log" "l2_rqsts.miss:u")"
  perf_llc_access="$(extract_perf_count "$perf_log" "longest_lat_cache.reference:u")"
  perf_llc_miss="$(extract_perf_count "$perf_log" "longest_lat_cache.miss:u")"

  local perf_l1_miss_rate
  local perf_l2_miss_rate
  local perf_llc_miss_rate
  perf_l1_miss_rate="$(format_percent "$perf_l1_miss" "$perf_l1_access")"
  perf_l2_miss_rate="$(format_percent "$perf_l2_miss" "$perf_l2_access")"
  perf_llc_miss_rate="$(format_percent "$perf_llc_miss" "$perf_llc_access")"

  echo "$perf_l1_miss_rate,$perf_l2_miss_rate,$perf_llc_miss_rate"
}

run_profiler_for_pid() {
  local pid="$1"
  local sample_interval_ms="$2"
  local measure_ms="$3"
  local results_dir="$4"
  local csv_file_name="$5"
  local terminal_log_enabled="$6"

  if [[ "$terminal_log_enabled" == "1" ]]; then
    sudo ./build/cache_profiler \
      --terminal-log \
      --csv-log \
      --csv-path "$results_dir" \
      --csv-filename "$csv_file_name" \
      "$pid" "$sample_interval_ms" "$measure_ms"
    return
  fi

  sudo ./build/cache_profiler \
    --csv-log \
    --csv-path "$results_dir" \
    --csv-filename "$csv_file_name" \
    "$pid" "$sample_interval_ms" "$measure_ms"
}

compute_profiler_miss_rates() {
  local profiler_csv_path="$1"
  if [[ ! -f "$profiler_csv_path" ]]; then
    echo "Profiler CSV output was not found at $profiler_csv_path" >&2
    return 1
  fi

  local last_row
  last_row="$(tail -n +2 "$profiler_csv_path" | tail -n 1)"
  if [[ -z "$last_row" ]]; then
    echo "Profiler CSV has no sample rows: $profiler_csv_path" >&2
    return 1
  fi

  local sample_idx
  local elapsed_ms
  local profiled_pid
  local l1_access
  local l1_miss
  local l2_access
  local l2_miss
  local llc_access
  local llc_miss
  IFS=',' read -r sample_idx elapsed_ms profiled_pid l1_access l1_miss l2_access l2_miss llc_access llc_miss <<< "$last_row"

  local profiler_l1_miss_rate
  local profiler_l2_miss_rate
  local profiler_llc_miss_rate
  profiler_l1_miss_rate="$(format_percent "$l1_miss" "$l1_access")"
  profiler_l2_miss_rate="$(format_percent "$l2_miss" "$l2_access")"
  profiler_llc_miss_rate="$(format_percent "$llc_miss" "$llc_access")"

  echo "$profiled_pid,$profiler_l1_miss_rate,$profiler_l2_miss_rate,$profiler_llc_miss_rate"
}

write_comparison_header() {
  local comparison_file="$1"
  echo "run,perf_pid,profiler_pid,perf_l1_miss_rate_pct,profiler_l1_miss_rate_pct,perf_l2_miss_rate_pct,profiler_l2_miss_rate_pct,perf_llc_miss_rate_pct,profiler_llc_miss_rate_pct" > "$comparison_file"
}
