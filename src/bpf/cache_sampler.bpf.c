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

static __always_inline void sampleEvent(void *pPerfMap, __u32 eventIdx, __u32 cpuIdx) {
    // Read current hardware counter value for this CPU.
    long perfValue = bpf_perf_event_read(pPerfMap, cpuIdx);
    __u64 *pTotal;

    if (perfValue < 0) {
        return;
    }

    pTotal = bpf_map_lookup_elem(&cache_totals, &eventIdx);
    if (pTotal == 0) {
        return;
    }

    // Keep latest observed value as the running total for this CPU/event.
    *pTotal = (__u64)perfValue;
}

SEC("tracepoint/sched/sched_switch")
int sampleOnSchedSwitch(void *pCtx) {
    __u32 targetPidKey = 0;
    __u32 *pTargetPid;
    __u64 currentPidTgid = bpf_get_current_pid_tgid();
    __u32 currentPid = (__u32)(currentPidTgid >> 32);
    __u32 cpuIdx = bpf_get_smp_processor_id();

    // Unused tracepoint context.
    (void)pCtx;

    // Filter to the requested PID only.
    pTargetPid = bpf_map_lookup_elem(&target_pid, &targetPidKey);
    if (pTargetPid == 0) {
        return 0;
    }

    if (currentPid != *pTargetPid) {
        return 0;
    }

    sampleEvent(&l1d_read_access_events, 0, cpuIdx);
    sampleEvent(&l1d_read_miss_events, 1, cpuIdx);
    sampleEvent(&l2_read_access_events, 2, cpuIdx);
    sampleEvent(&l2_read_miss_events, 3, cpuIdx);
    sampleEvent(&llc_read_access_events, 4, cpuIdx);
    sampleEvent(&llc_read_miss_events, 5, cpuIdx);

    return 0;
}
