# printmsg

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

## Run Example

```bash
./build/ebpf_main <pid> [interval_ms] [sample_count]
```

Example:

```bash
./build/ebpf_main 1234 500 10
```

This samples PID `1234` every `500` ms for `10` reads.

Output includes:
- `L1`: read accesses and misses
- `LLC`: read accesses and misses
- `L2`: reported as unsupported by default on generic Linux HW cache counters

## Library API

Public API is declared in `include/printmsg.h`:

- `printmsg_cache_sampler_create(pid, &sampler)`
- `printmsg_cache_sampler_read(sampler, &stats)`
- `printmsg_cache_sampler_destroy(sampler)`
