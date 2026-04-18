# cache_profiler

Tiny C library for sampling per-process cache counters.

## Requirements

- Linux with `perf_event_open` support
- Privileges to access performance counters for target PIDs
  - same-user PID: often works directly
  - system-wide/restricted setups: may require `sudo` or adjusted
    `kernel.perf_event_paranoid`

## Build

```bash
meson setup build
meson compile -C build
```

## Recompile After Changes

For normal source edits:

```bash
meson compile -C build
```

If you changed `meson.build`:

```bash
meson setup build --reconfigure
meson compile -C build
```

## Run

```bash
./build/cache_profiler <pid> [interval_ms] [sample_count]
```

Example:

```bash
./build/cache_profiler 1234 500 10
```

This samples PID `1234` every `500` ms for `10` reads.

Output includes:
- `L1`: read accesses and misses
- `LLC`: read accesses and misses
- `L2`: reported as unsupported by default on generic Linux HW cache counters

## Library API

Public API is declared in `include/cache_profiler.h`:

- `cache_profiler_iterate(pid, interval_ms, sample_count, callback, user_data)`
- `cache_profiler_capture(pid, interval_ms, sample_count, stats_array)`
- `cache_profiler_report(pid, interval_ms, sample_count, stats_array)`
- `cache_profiler_stream(pid, interval_ms, sample_count)`
- `cache_profiler_sampler_create(pid, &sampler)`
- `cache_profiler_sampler_read(sampler, &stats)`
- `cache_profiler_sampler_destroy(sampler)`

The profiler core gathers samples via `cache_profiler_iterate(...)` and
logging is handled separately by logger/report functions. This keeps output
formatting decoupled from profiling so future CSV sinks can be added cleanly.

Internally, this split is implemented as:
- `include/profiler.h` + `src/profiler.c` for sampling logic.
- `include/logger.h` + `src/logger.c` for text output logic.
- `include/cache_profiler.h` + `src/cache_profiler.c` as the stable facade API.
