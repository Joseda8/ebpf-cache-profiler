#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "CacheProfiler.h"

#include <errno.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <vector>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

namespace {

constexpr std::string_view kProgramName = "sampleOnSchedSwitch";
constexpr std::string_view kTargetPidMapName = "target_pid";
constexpr std::string_view kTotalsMapName = "cache_totals";

const std::array<std::string_view, 2> kPerfMapNames = {
    "l1d_read_access_events",
    "l1d_read_miss_events",
};

const std::array<uint64_t, 2> kPerfConfigs = {
    PERF_COUNT_HW_CACHE_L1D |
        (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
        (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16U),
    PERF_COUNT_HW_CACHE_L1D |
        (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
        (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16U),
};

int openPerfEventFd(int cpuIdx, uint64_t config) {
    struct perf_event_attr attr = {};

    // Configure one hardware cache counter for one CPU.
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_HW_CACHE;
    attr.config = config;
    attr.disabled = 0;
    attr.inherit = 0;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_idle = 1;

    return static_cast<int>(syscall(SYS_perf_event_open, &attr, -1, cpuIdx, -1, 0));
}

int doSampleOnce(pid_t targetPid, uint32_t sampleIntervalMs, const std::string& rBpfObjectPath, CacheSample& rSample) {
    // Initialize output so caller never sees stale values.
    rSample = {0, 0};

    // We create one perf counter per CPU per tracked event.
    long cpuCountLong = sysconf(_SC_NPROCESSORS_ONLN);
    int cpuCount = static_cast<int>(cpuCountLong);

    // Load maps/programs from the compiled eBPF object.
    auto objPtr = std::unique_ptr<bpf_object, decltype(&bpf_object__close)>(
        bpf_object__open_file(rBpfObjectPath.c_str(), nullptr), &bpf_object__close);
    if (!objPtr) {
        return -errno;
    }

    int err = bpf_object__load(objPtr.get());
    if (err != 0) {
        return err;
    }

    // Resolve required maps used by user-space setup and readback.
    int targetPidMapFd = bpf_object__find_map_fd_by_name(objPtr.get(), kTargetPidMapName.data());
    int totalsMapFd = bpf_object__find_map_fd_by_name(objPtr.get(), kTotalsMapName.data());
    if ((targetPidMapFd < 0) || (totalsMapFd < 0)) {
        return -ENOENT;
    }

    std::array<int, 2> perfMapFds = {-1, -1};
    for (size_t eventIdx = 0; eventIdx < kPerfMapNames.size(); ++eventIdx) {
        perfMapFds[eventIdx] = bpf_object__find_map_fd_by_name(objPtr.get(), kPerfMapNames[eventIdx].data());
        if (perfMapFds[eventIdx] < 0) {
            return -ENOENT;
        }
    }

    uint32_t targetPidMapKey = 0;
    uint32_t targetPidValue = static_cast<uint32_t>(targetPid);
    // Tell eBPF which PID should be sampled.
    if (bpf_map_update_elem(targetPidMapFd, &targetPidMapKey, &targetPidValue, BPF_ANY) != 0) {
        return -errno;
    }

    // Start each sampling window from zero.
    std::vector<uint64_t> perCpuZeros(static_cast<size_t>(cpuCount), 0);
    for (uint32_t eventIdx = 0; eventIdx < 2; ++eventIdx) {
        if (bpf_map_update_elem(totalsMapFd, &eventIdx, perCpuZeros.data(), BPF_ANY) != 0) {
            return -errno;
        }
    }

    std::vector<int> perfFds;
    perfFds.reserve(static_cast<size_t>(cpuCount * 2));

    for (uint32_t eventIdx = 0; eventIdx < 2; ++eventIdx) {
        std::vector<int> eventFds;
        eventFds.reserve(static_cast<size_t>(cpuCount));

        // Open one perf counter fd per CPU for this event type.
        for (int cpuIdx = 0; cpuIdx < cpuCount; ++cpuIdx) {
            int perfFd = openPerfEventFd(cpuIdx, kPerfConfigs[eventIdx]);
            if (perfFd < 0) {
                return -errno;
            }
            eventFds.push_back(perfFd);
            perfFds.push_back(perfFd);
        }

        for (int cpuIdx = 0; cpuIdx < cpuCount; ++cpuIdx) {
            int perfFd = eventFds[cpuIdx];
            // Bind each perf fd into the corresponding PERF_EVENT_ARRAY map slot.
            // Current blocker on this machine: this update returns EOPNOTSUPP.
            if ((ioctl(perfFd, PERF_EVENT_IOC_RESET, 0) != 0) ||
                (ioctl(perfFd, PERF_EVENT_IOC_ENABLE, 0) != 0) ||
                (bpf_map_update_elem(perfMapFds[eventIdx], &cpuIdx, &perfFd, BPF_ANY) != 0)) {
                return -errno;
            }
        }
    }

    // Attach tracepoint program that reads perf counters in-kernel.
    bpf_program* pProgram = bpf_object__find_program_by_name(objPtr.get(), kProgramName.data());
    if (!pProgram) {
        return -ENOENT;
    }

    auto linkPtr = std::unique_ptr<bpf_link, decltype(&bpf_link__destroy)>(
        bpf_program__attach(pProgram), &bpf_link__destroy);
    err = static_cast<int>(libbpf_get_error(linkPtr.get()));
    if (err == -EOPNOTSUPP) {
        linkPtr.reset(bpf_program__attach_tracepoint(pProgram, "sched", "sched_switch"));
        err = static_cast<int>(libbpf_get_error(linkPtr.get()));
    }
    if (err != 0) {
        linkPtr.release();
        return err;
    }

    // Sampling interval.
    usleep(sampleIntervalMs * 1000U);

    // Read per-CPU totals and sum them for output.
    uint32_t accessKey = 0;
    uint32_t missKey = 1;
    std::vector<uint64_t> perCpuValues(static_cast<size_t>(cpuCount), 0);

    if (bpf_map_lookup_elem(totalsMapFd, &accessKey, perCpuValues.data()) != 0) {
        return -errno;
    }
    for (uint64_t value : perCpuValues) {
        rSample.l1ReadAccesses += value;
    }

    std::fill(perCpuValues.begin(), perCpuValues.end(), 0);
    if (bpf_map_lookup_elem(totalsMapFd, &missKey, perCpuValues.data()) != 0) {
        return -errno;
    }
    for (uint64_t value : perCpuValues) {
        rSample.l1ReadMisses += value;
    }

    for (int perfFd : perfFds) {
        close(perfFd);
    }

    return 0;
}

}  // namespace

int CacheProfiler::sampleOnce(pid_t targetPid, uint32_t sampleIntervalMs, const std::string& rBpfObjectPath, CacheSample& rSampleOutput) {
    return doSampleOnce(targetPid, sampleIntervalMs, rBpfObjectPath, rSampleOutput);
}
