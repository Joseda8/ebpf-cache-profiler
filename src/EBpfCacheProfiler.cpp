#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "EBpfCacheProfiler.h"

#include <errno.h>
#include <linux/perf_event.h>
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

enum class CacheEventIdx : uint32_t {
    kL1ReadAccess = 0,
    kL1ReadMiss = 1,
    kL2ReadAccess = 2,
    kL2ReadMiss = 3,
    kLlcReadAccess = 4,
    kLlcReadMiss = 5,
};

struct PerfEventSpec {
    std::string_view mapName;
    perf_type_id perfType;
    uint64_t config;
    CacheEventIdx eventIdx;
};

constexpr uint64_t kL2RqstsReferencesConfig = 0xFF24;
constexpr uint64_t kL2RqstsMissesConfig = 0x3F24;

const std::array<PerfEventSpec, 6> kPerfEventSpecs = {{
    {"l1d_read_access_events",
     PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_L1D |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16U),
     CacheEventIdx::kL1ReadAccess},
    {"l1d_read_miss_events",
     PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_L1D |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16U),
     CacheEventIdx::kL1ReadMiss},
    {"l2_read_access_events",
     PERF_TYPE_RAW,
     kL2RqstsReferencesConfig,
     CacheEventIdx::kL2ReadAccess},
    {"l2_read_miss_events",
     PERF_TYPE_RAW,
     kL2RqstsMissesConfig,
     CacheEventIdx::kL2ReadMiss},
    {"llc_read_access_events",
     PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_LL |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16U),
     CacheEventIdx::kLlcReadAccess},
    {"llc_read_miss_events",
     PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_LL |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16U),
     CacheEventIdx::kLlcReadMiss},
}};

int openPerfEventFd(int cpuIdx, perf_type_id perfType, uint64_t config) {
    struct perf_event_attr attr = {};

    // Configure one hardware cache counter for one CPU.
    attr.size = sizeof(attr);
    attr.type = perfType;
    attr.config = config;
    attr.disabled = 0;
    attr.inherit = 0;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_idle = 1;

    return static_cast<int>(syscall(SYS_perf_event_open, &attr, -1, cpuIdx, -1, 0));
}

int sumTotalsForEvent(int totalsMapFd, CacheEventIdx eventIdx, int cpuCount, uint64_t& rOutput) {
    rOutput = 0;
    std::vector<uint64_t> perCpuValues(static_cast<size_t>(cpuCount), 0);
    uint32_t mapKey = static_cast<uint32_t>(eventIdx);

    if (bpf_map_lookup_elem(totalsMapFd, &mapKey, perCpuValues.data()) != 0) {
        return -errno;
    }

    for (uint64_t value : perCpuValues) {
        rOutput += value;
    }

    return 0;
}

}  // namespace

EBpfCacheProfiler::EBpfCacheProfiler(const std::string& rBpfObjectPath) : bpfObjectPath(rBpfObjectPath) {}

int EBpfCacheProfiler::sampleOnce(pid_t targetPid, uint32_t sampleIntervalMs, CacheSample& rSampleOutput) {
    // Initialize output so caller never sees stale values.
    rSampleOutput = {0, 0, 0, 0, 0, 0};

    // We create one perf counter per CPU per tracked event.
    long cpuCountLong = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpuCountLong <= 0) {
        return -EINVAL;
    }
    int cpuCount = static_cast<int>(cpuCountLong);

    // Load maps/programs from the compiled eBPF object.
    auto objPtr = std::unique_ptr<bpf_object, decltype(&bpf_object__close)>(
        bpf_object__open_file(bpfObjectPath.c_str(), nullptr), &bpf_object__close);
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

    std::array<int, kPerfEventSpecs.size()> perfMapFds = {};
    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        perfMapFds[eventSpecIdx] = bpf_object__find_map_fd_by_name(objPtr.get(), kPerfEventSpecs[eventSpecIdx].mapName.data());
        if (perfMapFds[eventSpecIdx] < 0) {
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
    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        uint32_t totalsMapKey = static_cast<uint32_t>(kPerfEventSpecs[eventSpecIdx].eventIdx);
        if (bpf_map_update_elem(totalsMapFd, &totalsMapKey, perCpuZeros.data(), BPF_ANY) != 0) {
            return -errno;
        }
    }

    std::vector<int> perfFds;
    perfFds.reserve(static_cast<size_t>(cpuCount * kPerfEventSpecs.size()));

    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        std::vector<int> eventFds;
        eventFds.reserve(static_cast<size_t>(cpuCount));

        // Open one perf counter fd per CPU for this event type.
        for (int cpuIdx = 0; cpuIdx < cpuCount; ++cpuIdx) {
            int perfFd = openPerfEventFd(cpuIdx, kPerfEventSpecs[eventSpecIdx].perfType, kPerfEventSpecs[eventSpecIdx].config);
            if (perfFd < 0) {
                return -errno;
            }
            eventFds.push_back(perfFd);
            perfFds.push_back(perfFd);
        }

        for (int cpuIdx = 0; cpuIdx < cpuCount; ++cpuIdx) {
            int perfFd = eventFds[cpuIdx];

            // Bind each perf fd into the corresponding PERF_EVENT_ARRAY map slot.
            if ((ioctl(perfFd, PERF_EVENT_IOC_RESET, 0) != 0) ||
                (ioctl(perfFd, PERF_EVENT_IOC_ENABLE, 0) != 0) ||
                (bpf_map_update_elem(perfMapFds[eventSpecIdx], &cpuIdx, &perfFd, BPF_ANY) != 0)) {
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

    err = sumTotalsForEvent(totalsMapFd, CacheEventIdx::kL1ReadAccess, cpuCount, rSampleOutput.l1ReadAccesses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(totalsMapFd, CacheEventIdx::kL1ReadMiss, cpuCount, rSampleOutput.l1ReadMisses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(totalsMapFd, CacheEventIdx::kL2ReadAccess, cpuCount, rSampleOutput.l2ReadAccesses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(totalsMapFd, CacheEventIdx::kL2ReadMiss, cpuCount, rSampleOutput.l2ReadMisses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(totalsMapFd, CacheEventIdx::kLlcReadAccess, cpuCount, rSampleOutput.llcReadAccesses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(totalsMapFd, CacheEventIdx::kLlcReadMiss, cpuCount, rSampleOutput.llcReadMisses);
    if (err != 0) {
        return err;
    }

    for (int perfFd : perfFds) {
        close(perfFd);
    }

    return 0;
}
