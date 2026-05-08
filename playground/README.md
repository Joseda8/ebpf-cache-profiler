# Playground
Reusable experiment harness for comparing `perf stat` against `cache_profiler`.

## Structure
- `lib/experiment_common.sh`: shared parsing, rate computation, perf/profiler runners.
- `workloads/`: reusable workload programs.
- `experiments/`: experiment entrypoints.

## Run
- Perf overhead experiments may require root privileges for `perf` event access on this machine.
- Python workload experiment:
  - `./playground/experiments/python_vs_profiler.sh`
- Multi-threaded workload experiment (non-Python):
  - `./playground/experiments/threaded_vs_profiler.sh`
- Python BST slowdown experiment (baseline vs perf vs profiler):
  - `./playground/experiments/python_bst_slowdown_vs_profiler.sh`
- Perf overhead on pyperformance:
  - `./playground/experiments/perf_overhead_pyperformance.sh`
- Perf overhead on NAS Parallel Benchmarks:
  - `NPB_BIN_DIR=/path/to/NPB/bin ./playground/experiments/perf_overhead_npb.sh`

python3 ./playground/tools/plot_value_distributions.py --results-dir ./playground/results/python_vs_profiler
python3 ./playground/tools/plot_value_distributions.py --results-dir ./playground/results/threaded_vs_profiler

python3 ./playground/tools/summarize_perf_overhead.py \
  --input-csv ./playground/results/perf_overhead_pyperformance/perf_overhead.csv \
  --input-csv ./playground/results/perf_overhead_npb/perf_overhead.csv \
  --output ./playground/results/perf_overhead_summary.csv
