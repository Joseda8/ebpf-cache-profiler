#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

#define EVENT_COUNT 6
#define MAX_CPU_COUNT 512

char LICENSE[] SEC("license") = "GPL";

// PID selector written by user space.
struct bpf_map_def SEC("maps") target_pid = {
    .type = BPF_MAP_TYPE_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = 1,
};

// Per-CPU totals for:
// key 0 -> L1 read accesses
// key 1 -> L1 read misses
// key 2 -> L2 read accesses
// key 3 -> L2 read misses
// key 4 -> LLC read accesses
// key 5 -> LLC read misses
struct bpf_map_def SEC("maps") cache_totals = {
    .type = BPF_MAP_TYPE_PERCPU_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u64),
    .max_entries = EVENT_COUNT,
};

// Per-CPU previous raw perf values for delta computation.
// key 0..5 match cache_totals key layout.
struct bpf_map_def SEC("maps") prev_cache_values = {
    .type = BPF_MAP_TYPE_PERCPU_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u64),
    .max_entries = EVENT_COUNT,
};

// Perf fd table for L1 read-access counters, indexed by CPU.
struct bpf_map_def SEC("maps") l1d_read_access_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = MAX_CPU_COUNT,
};

// Perf fd table for L1 read-miss counters, indexed by CPU.
struct bpf_map_def SEC("maps") l1d_read_miss_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = MAX_CPU_COUNT,
};

// Perf fd table for L2 read-access counters, indexed by CPU.
struct bpf_map_def SEC("maps") l2_read_access_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = MAX_CPU_COUNT,
};

// Perf fd table for L2 read-miss counters, indexed by CPU.
struct bpf_map_def SEC("maps") l2_read_miss_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = MAX_CPU_COUNT,
};

// Perf fd table for LLC read-access counters, indexed by CPU.
struct bpf_map_def SEC("maps") llc_read_access_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = MAX_CPU_COUNT,
};

// Perf fd table for LLC read-miss counters, indexed by CPU.
struct bpf_map_def SEC("maps") llc_read_miss_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = MAX_CPU_COUNT,
};

static __always_inline void sampleEvent(void *pPerfMap, __u32 eventIdx, __u32 cpuIdx, __u32 shouldAccumulate) {
    // PMU counters are cumulative, so each sched_switch sees a raw running total.
    // We only want new work since the previous switch, attributed to the target process.
    //
    // Example (single CPU/event, target process is "B"):
    // - Switch sees A: raw=100, prev=0   -> delta=100, A != target, add 0,  prev=100
    // - Switch sees B: raw=160, prev=100 -> delta=60,  B == target, add 60, prev=160
    // - Switch sees C: raw=220, prev=160 -> delta=60, C != target, add 0,   prev=220
    // - Switch sees B: raw=260, prev=220 -> delta=40,  B == target, add 40, prev=260
    // Final target total is 100 (60 + 40), not raw 260.

    // Read current hardware counter value for this CPU.
    long perfValue = bpf_perf_event_read(pPerfMap, cpuIdx);
    __u64 *pPrevValue;

    if (perfValue < 0) {
        return;
    }

    pPrevValue = bpf_map_lookup_elem(&prev_cache_values, &eventIdx);
    if (pPrevValue == 0) {
        return;
    }

    __u64 currentValue = (__u64)perfValue;
    __u64 deltaValue = 0;
    // Convert cumulative PMU value into per-switch delta.
    if (currentValue >= *pPrevValue) {
        deltaValue = currentValue - *pPrevValue;
    }

    // Always advance previous value so next sched_switch uses a fresh baseline.
    *pPrevValue = currentValue;

    if ((shouldAccumulate == 0) || (deltaValue == 0)) {
        return;
    }

    __u64 *pTotal = bpf_map_lookup_elem(&cache_totals, &eventIdx);
    if (pTotal == 0) {
        return;
    }

    // Accumulate only the newly observed delta for this switch window.
    *pTotal += deltaValue;
}

SEC("tracepoint/sched/sched_switch")
int sampleOnSchedSwitch(void *pCtx) {
    __u32 targetPidKey = 0;
    __u32 *pTargetPid;
    __u64 currentPidTgid = bpf_get_current_pid_tgid();
    __u32 currentTgid = (__u32)(currentPidTgid >> 32);
    __u32 cpuIdx = bpf_get_smp_processor_id();
    __u32 shouldAccumulate = 0;

    // Unused tracepoint context.
    (void)pCtx;

    // Filter to the requested PID only.
    pTargetPid = bpf_map_lookup_elem(&target_pid, &targetPidKey);
    if (pTargetPid == 0) {
        return 0;
    }

    shouldAccumulate = (currentTgid == *pTargetPid) ? 1 : 0;

    sampleEvent(&l1d_read_access_events, 0, cpuIdx, shouldAccumulate);
    sampleEvent(&l1d_read_miss_events, 1, cpuIdx, shouldAccumulate);
    sampleEvent(&l2_read_access_events, 2, cpuIdx, shouldAccumulate);
    sampleEvent(&l2_read_miss_events, 3, cpuIdx, shouldAccumulate);
    sampleEvent(&llc_read_access_events, 4, cpuIdx, shouldAccumulate);
    sampleEvent(&llc_read_miss_events, 5, cpuIdx, shouldAccumulate);

    return 0;
}
