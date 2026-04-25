# cache_sampler

C++ cache-profiler library and CLI built on eBPF.

Current scope:
- L1 read accesses and misses
- L2 read accesses and misses
- LLC read accesses and misses

## Build

```bash
meson setup build
meson compile -C build
```

## Run

```bash
sudo ./build/cache_profiler [options] <pid> <interval_ms> [duration_ms]
```

- Options must come before positional arguments.
- `--terminal-log`: enables terminal output.
- `--csv-log`: enables CSV output mode.
- `--csv-path <dir>`: CSV output directory (default: current directory).
- `--csv-filename <name>`: CSV file name.
  - Default when omitted: `YYYYMMDDTHHMMSS_PID`.
- `--csv-flush-samples <count>`: number of samples buffered before file flush (default: `10`).
- `--terminal-log` and CSV options can be used together to log to terminal and CSV simultaneously.
- `interval_ms`: sampling period between emitted cumulative snapshots.
- `duration_ms` (optional): total profiler runtime. If omitted, profiling continues until the profiler is stopped or the target PID exits.
- Optional log level control: set `CACHE_PROFILER_LOG_LEVEL` to `debug`, `info`, `warning`, or `error` (default: `info`).
- Current sample semantics: each metric is cumulative since profiling initialization (not interval delta).

## Library API

Public headers:
- `include/CacheProfilerApp.h`
- `include/CacheSampleLoggerConfig.h`
- `include/ProfilingConfig.h`
- `include/ICacheProfiler.h`
- `include/EBpfCacheProfiler.h`
- `include/CacheSample.h`
- `include/ICacheSampleLogger.h`
- `include/TerminalCacheSampleLogger.h`
- `include/CsvCacheSampleLogger.h`

Runtime split:
- `CacheProfilerApp` is the library entry point that runs periodic sampling.
- `ICacheProfiler` implementations only produce samples.
- `ICacheSampleLogger` implementations decide how samples are emitted (terminal now, CSV later).
- The CLI client (`src/client/Main.cpp`) only parses CLI arguments and invokes the library API.

L2 and LLC events are currently opened as raw PMU events (model-specific encodings such as `L2_RQSTS` and `longest_lat_cache.*`), so kernel/CPU support is required.

Note on map compatibility:
- BPF maps are declared using legacy `bpf_map_def` for compatibility with this machine's kernel/libbpf map-create behavior.
- Profiling still runs in-kernel via eBPF (`tracepoint` + `bpf_perf_event_read`).
- Tradeoff: reduced BTF-based map metadata/introspection compared to modern BTF-style map declarations.
