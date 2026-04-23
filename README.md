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
sudo ./build/cache_profiler <pid> <interval_ms> ./build/cache_sampler.bpf.o
```

## Library API

Public headers:
- `include/ICacheProfiler.h`
- `include/EBpfCacheProfiler.h`
- `include/CacheSample.h`

L2 events are currently opened as raw PMU events (`L2_RQSTS` references/misses), so kernel/CPU support is required.
