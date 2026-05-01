# Playground
Reusable experiment harness for comparing `perf stat` against `cache_profiler`.

## Structure
- `lib/experiment_common.sh`: shared parsing, rate computation, perf/profiler runners.
- `workloads/`: reusable workload programs.
- `experiments/`: experiment entrypoints.

## Run
- Python workload experiment:
  - `./playground/experiments/python_vs_profiler.sh`
- Multi-threaded workload experiment (non-Python):
  - `./playground/experiments/threaded_vs_profiler.sh`
- Python BST slowdown experiment (baseline vs perf vs profiler):
  - `./playground/experiments/python_bst_slowdown_vs_profiler.sh`

python3 ./playground/tools/plot_value_distributions.py --results-dir ./playground/results/python_vs_profiler
python3 ./playground/tools/plot_value_distributions.py --results-dir ./playground/results/threaded_vs_profiler
