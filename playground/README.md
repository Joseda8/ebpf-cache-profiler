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
