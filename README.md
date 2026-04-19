# cache_sampler

Proof-of-concept C++ cache profiler using eBPF.

Current scope:
- L1 data-cache read accesses
- L1 data-cache read misses

## Build

```bash
meson setup build
meson compile -C build
```

## Run

```bash
sudo ./build/cache_profiler
```
