# Playground
Reusable experiment harness focused on measuring profiler overhead (`perf` or eBPF).

## Structure
- `lib/measure_profiler_overhead.sh`: core runner that records baseline target metrics, profiled target metrics, and profiler-process metrics.
- `experiments/perf_overhead_pyperformance.sh`: pyperformance-specific wrapper around `lib/measure_profiler_overhead.sh`.
- `experiments/perf_overhead_npb.sh`: NPB-specific wrapper around `lib/measure_profiler_overhead.sh`.
- `experiments/perf_overhead_local_workloads.sh`: local-workloads wrapper around `lib/measure_profiler_overhead.sh` (Python BST + threaded C++).
- `workloads/`: reusable workloads.
- `results/`: generated outputs.

## Run
- Generic measurement for any command:
  - `./playground/lib/measure_profiler_overhead.sh -- <target_command> [args ...]`
  - Optional knobs: `PROFILER_BACKEND` (`all`, `perf`, or `ebpf`), `RUN_COUNT`, `ATTACH_GRACE_SECONDS`
  - `ebpf` knobs: `EBPF_PROFILER_BIN`, `EBPF_SAMPLE_INTERVAL_MS`
- pyperformance wrapper:
  - `./playground/experiments/perf_overhead_pyperformance.sh`
  - Optional knobs: `RUN_COUNT`, `PYPERFORMANCE_BENCHMARKS`, `PYPERFORMANCE_RUN_MODE`, `RESULTS_ROOT`, `PROFILER_BACKEND`
- NPB wrapper:
  - `NPB_BIN_DIR=/path/to/NPB/bin ./playground/experiments/perf_overhead_npb.sh`
  - Optional knobs: `RUN_COUNT`, `NPB_BENCHMARKS`, `NPB_CLASS`, `NPB_MPI_PROCS`, `RESULTS_ROOT`, `PROFILER_BACKEND`
- Local workloads wrapper:
  - `./playground/experiments/perf_overhead_local_workloads.sh`
  - Optional knobs: `RUN_COUNT`, `PYTHON_NODE_COUNT`, `THREADED_SLEEP_SECONDS`, `THREADED_COMPUTE_SECONDS`, `THREADED_THREAD_COUNT`, `RESULTS_ROOT`, `PROFILER_BACKEND`

## Output
- Each run writes one `raw_process_metrics.csv` file with columns:
  - `run,scenario,process,wall_seconds,user_cpu_seconds,sys_cpu_seconds,maxrss_kb,exit_code`
  - `scenario` is `baseline` for baseline rows and `profiled_<backend>` for profiled rows.
- Each run also writes `profiler_stats.csv`:
  - `perf` backend: one row per perf event count per run.
  - `ebpf` backend: one row per final cumulative sample metric per run.
- Default result roots are backend-aware:
  - `all_*` paths when `PROFILER_BACKEND=all`
  - `perf_*` paths when `PROFILER_BACKEND=perf`
  - `ebpf_*` paths when `PROFILER_BACKEND=ebpf`
