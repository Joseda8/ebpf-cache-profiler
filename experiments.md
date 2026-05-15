```bash
sudo -v

# Local workloads (perf)
sudo nohup env PROFILER_BACKEND=perf RUN_COUNT=10 ./playground/experiments/perf_overhead_local_workloads.sh > /tmp/perf_overhead_local_perf.log 2>&1 &
echo $!
1563097

# Local workloads (eBPF)
sudo nohup env PROFILER_BACKEND=ebpf RUN_COUNT=10 EBPF_SAMPLE_INTERVAL_MS=200 ./playground/experiments/perf_overhead_local_workloads.sh > /tmp/perf_overhead_local_ebpf.log 2>&1 &
echo $!
1566491

# pyperformance workloads (perf)
sudo nohup env PROFILER_BACKEND=perf RUN_COUNT=10 ./playground/experiments/perf_overhead_pyperformance.sh > /tmp/perf_overhead_pyperf_perf.log 2>&1 &
echo $!
1570968

# pyperformance workloads (eBPF)
sudo nohup env PROFILER_BACKEND=ebpf RUN_COUNT=10 EBPF_SAMPLE_INTERVAL_MS=200 ./playground/experiments/perf_overhead_pyperformance.sh > /tmp/perf_overhead_pyperf_ebpf.log 2>&1 &
echo $!
1676726

# NPB workloads (perf)
sudo nohup env NPB_BIN_DIR=/home/jmontoya/NPB3.4.3/NPB3.4-OMP/bin PROFILER_BACKEND=perf RUN_COUNT=10 ./playground/experiments/perf_overhead_npb.sh > /tmp/perf_overhead_npb_perf.log 2>&1 &
echo $!
1782805

# NPB workloads (eBPF) *
sudo nohup env NPB_BIN_DIR=/home/jmontoya/NPB3.4.3/NPB3.4-OMP/bin PROFILER_BACKEND=ebpf RUN_COUNT=10 EBPF_SAMPLE_INTERVAL_MS=200 ./playground/experiments/perf_overhead_npb.sh > /tmp/perf_overhead_npb_ebpf.log 2>&1 &
echo $!
1787234

# Monitor progress (run now or after reconnect)
tail -f /tmp/perf_overhead_local_perf.log
tail -f /tmp/perf_overhead_local_ebpf.log
tail -f /tmp/perf_overhead_pyperf_perf.log
tail -f /tmp/perf_overhead_pyperf_ebpf.log
tail -f /tmp/perf_overhead_npb_perf.log
tail -f /tmp/perf_overhead_npb_ebpf.log
```
