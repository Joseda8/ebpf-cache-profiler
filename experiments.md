```bash
# From repo root.

# ------------------------------
# 1) Local workloads
# Baseline + perf + eBPF in one command (default backend mode is all).
sudo nohup env RUN_COUNT=10 ./playground/experiments/perf_overhead_local_workloads.sh > /tmp/perf_overhead_local_all.log 2>&1 &
echo $!
1848652

# ------------------------------
# 2) pyperformance workloads
# Baseline + perf + eBPF in one command.
# Adjust benchmarks with PYPERFORMANCE_BENCHMARKS if needed.
sudo nohup env RUN_COUNT=10 PYPERFORMANCE_RUN_MODE=rigorous ./playground/experiments/perf_overhead_pyperformance.sh > /tmp/perf_overhead_pyperf_all.log 2>&1 &
echo $!
1854027

# ------------------------------
# 3) NPB workloads
# Baseline + perf + eBPF in one command.
sudo nohup env RUN_COUNT=10 NPB_BIN_DIR=/home/jmontoya/NPB3.4.3/NPB3.4-OMP/bin ./playground/experiments/perf_overhead_npb.sh > /tmp/perf_overhead_npb_all.log 2>&1 &
echo $!
2013614

# ------------------------------
# Monitor logs

tail -f /tmp/perf_overhead_local_all.log
tail -f /tmp/perf_overhead_pyperf_all.log
tail -f /tmp/perf_overhead_npb_all.log

# ------------------------------
# Optional: force a single backend if desired
# (normally not needed, because default is all)
#
# sudo nohup env PROFILER_BACKEND=perf ...
# sudo nohup env PROFILER_BACKEND=ebpf ...
```
