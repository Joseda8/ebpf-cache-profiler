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
#include <cstdarg>
#include <cstdio>
#include <cstring>
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
    // PERF_EVENT_ARRAY map name in the BPF object.
    std::string_view mapName;

    // perf_event_attr.type value used when opening the event.
    perf_type_id perfType;

    // perf_event_attr.config value.
    uint64_t config;

    // perf_event_attr.config1 value (extra model-specific filter bits when needed).
    uint64_t config1;

    // perf_event_attr.config2 value (reserved for events that require it).
    uint64_t config2;

    // Key slot in cache_totals map used for this event.
    CacheEventIdx eventIdx;
};

// Raw PMU encodings validated on this target machine with `perf list --details`.
// Encoding follows event-select format: config = event | (umask << 8).
// These values are model-specific and may need updates on different CPUs.
constexpr uint64_t kL2RqstsReferencesConfig = 0xFF24;
constexpr uint64_t kL2RqstsMissesConfig = 0x3F24;
constexpr uint64_t kLongestLatCacheReferenceConfig = 0x4F2E;
constexpr uint64_t kLongestLatCacheMissConfig = 0x412E;
constexpr uint64_t kLongestLatCacheExtraFilterConfig1 = 0x186A3;

const std::array<PerfEventSpec, 6> kPerfEventSpecs = {{
    {"l1d_read_access_events",
     PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_L1D |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16U),
     0,
     0,
     CacheEventIdx::kL1ReadAccess},
    {"l1d_read_miss_events",
     PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_L1D |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ) << 8U) |
         (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16U),
     0,
     0,
     CacheEventIdx::kL1ReadMiss},
    {"l2_read_access_events",
     PERF_TYPE_RAW,
     kL2RqstsReferencesConfig,
     0,
     0,
     CacheEventIdx::kL2ReadAccess},
    {"l2_read_miss_events",
     PERF_TYPE_RAW,
     kL2RqstsMissesConfig,
     0,
     0,
     CacheEventIdx::kL2ReadMiss},
    {"llc_read_access_events",
     PERF_TYPE_RAW,
     kLongestLatCacheReferenceConfig,
     kLongestLatCacheExtraFilterConfig1,
     0,
     CacheEventIdx::kLlcReadAccess},
    {"llc_read_miss_events",
     PERF_TYPE_RAW,
     kLongestLatCacheMissConfig,
     kLongestLatCacheExtraFilterConfig1,
     0,
     CacheEventIdx::kLlcReadMiss},
}};

int openPerfEventFd(int cpuIdx, perf_type_id perfType, uint64_t config, uint64_t config1, uint64_t config2) {
    struct perf_event_attr attr = {};

    // Configure one hardware cache counter for one CPU.
    attr.size = sizeof(attr);
    attr.type = perfType;
    attr.config = config;
    attr.config1 = config1;
    attr.config2 = config2;
    attr.disabled = 0;
    attr.inherit = 0;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_idle = 1;

    return static_cast<int>(syscall(SYS_perf_event_open, &attr, -1, cpuIdx, -1, 0));
}

int sumTotalsForEvent(int _totalsMapFd, CacheEventIdx eventIdx, int _cpuCount, uint64_t& rOutput) {
    rOutput = 0;
    std::vector<uint64_t> perCpuValues(static_cast<size_t>(_cpuCount), 0);
    uint32_t mapKey = static_cast<uint32_t>(eventIdx);

    if (bpf_map_lookup_elem(_totalsMapFd, &mapKey, perCpuValues.data()) != 0) {
        return -errno;
    }

    for (uint64_t value : perCpuValues) {
        rOutput += value;
    }

    return 0;
}

void closePerfFds(std::vector<int>& rPerfFds) {
    for (int perfFd : rPerfFds) {
        close(perfFd);
    }
    rPerfFds.clear();
}

int libbpfLogCallback(enum libbpf_print_level level, const char* pFormat, va_list args) {
    // Keep the callback robust for unexpected invocations.
    if (pFormat == nullptr) {
        return 0;
    }

    char messageBuffer[1024] = {};
    va_list argsCopy;
    va_copy(argsCopy, args);
    int formatResult = vsnprintf(messageBuffer, sizeof(messageBuffer), pFormat, argsCopy);
    va_end(argsCopy);
    if (formatResult < 0) {
        return 0;
    }

    const bool isBtfRetryMessage = (strstr(messageBuffer, "Error in bpf_create_map_xattr(") != nullptr) &&
                                    (strstr(messageBuffer, "Retrying without BTF.") != nullptr);
    if (isBtfRetryMessage) {
        // Some kernel/libbpf combinations reject map creation with BTF metadata
        // and transparently retry map creation without BTF. Profiling remains
        // kernel-level eBPF in this mode, but map type metadata is unavailable.
        return 0;
    }

    if (level == LIBBPF_DEBUG) {
        return 0;
    }

    return fprintf(stderr, "%s", messageBuffer);
}

}  // namespace

EBpfCacheProfiler::EBpfCacheProfiler(const std::string& rBpfObjectPath)
    : _bpfObjectPath(rBpfObjectPath),
      _bpfObjectPtr(nullptr, &bpf_object__close),
      _bpfLinkPtr(nullptr, &bpf_link__destroy),
      _perfMapFds({-1, -1, -1, -1, -1, -1}),
      _targetPidMapFd(-1),
      _totalsMapFd(-1),
      _cpuCount(0),
      _isInitialized(false) {}

EBpfCacheProfiler::~EBpfCacheProfiler() {
    closePerfFds(_perfFds);
}

int EBpfCacheProfiler::sampleOnce(pid_t targetPid, uint32_t sampleIntervalMs, CacheSample& rSampleOutput) {
    // Load and attach eBPF resources only once per profiler instance.
    int err = initializeOnce();
    if (err != 0) {
        return err;
    }

    // Update target PID selector before each sampling window.
    err = configureTargetPid(targetPid);
    if (err != 0) {
        return err;
    }

    // Reset both perf counters and totals map to bound the next interval.
    err = resetPerfCounters();
    if (err != 0) {
        return err;
    }

    err = resetTotals();
    if (err != 0) {
        return err;
    }

    // Keep counters enabled during the requested interval length.
    usleep(sampleIntervalMs * 1000U);

    return readTotals(rSampleOutput);
}

int EBpfCacheProfiler::initializeOnce() {
    if (_isInitialized) {
        return 0;
    }

    // Install log filtering for the known BTF-map fallback noise.
    // This does not disable eBPF sampling; it only suppresses that startup line.
    libbpf_set_print(libbpfLogCallback);

    // Create one perf counter per CPU for each tracked event type.
    long cpuCountLong = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpuCountLong <= 0) {
        return -EINVAL;
    }
    _cpuCount = static_cast<int>(cpuCountLong);

    // Open and load maps/programs from the compiled eBPF object.
    auto objectPtr = std::unique_ptr<bpf_object, decltype(&bpf_object__close)>(
        bpf_object__open_file(_bpfObjectPath.c_str(), nullptr), &bpf_object__close);
    int err = static_cast<int>(libbpf_get_error(objectPtr.get()));
    if (err != 0) {
        objectPtr.release();
        return err;
    }

    err = bpf_object__load(objectPtr.get());
    if (err != 0) {
        return err;
    }

    // Resolve map FDs required by userspace setup and result readback.
    _targetPidMapFd = bpf_object__find_map_fd_by_name(objectPtr.get(), kTargetPidMapName.data());
    _totalsMapFd = bpf_object__find_map_fd_by_name(objectPtr.get(), kTotalsMapName.data());
    if ((_targetPidMapFd < 0) || (_totalsMapFd < 0)) {
        return -ENOENT;
    }

    std::array<int, kPerfEventSpecs.size()> localPerfMapFds = {};
    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        localPerfMapFds[eventSpecIdx] = bpf_object__find_map_fd_by_name(objectPtr.get(), kPerfEventSpecs[eventSpecIdx].mapName.data());
        if (localPerfMapFds[eventSpecIdx] < 0) {
            return -ENOENT;
        }
    }

    std::vector<int> localPerfFds;
    localPerfFds.reserve(static_cast<size_t>(_cpuCount * kPerfEventSpecs.size()));

    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        std::vector<int> eventFds;
        eventFds.reserve(static_cast<size_t>(_cpuCount));

        // Open one perf counter fd per CPU for this event type.
        for (int cpuIdx = 0; cpuIdx < _cpuCount; ++cpuIdx) {
            int perfFd = openPerfEventFd(
                cpuIdx,
                kPerfEventSpecs[eventSpecIdx].perfType,
                kPerfEventSpecs[eventSpecIdx].config,
                kPerfEventSpecs[eventSpecIdx].config1,
                kPerfEventSpecs[eventSpecIdx].config2);
            if (perfFd < 0) {
                closePerfFds(localPerfFds);
                return -errno;
            }
            eventFds.push_back(perfFd);
            localPerfFds.push_back(perfFd);
        }

        for (int cpuIdx = 0; cpuIdx < _cpuCount; ++cpuIdx) {
            int perfFd = eventFds[cpuIdx];

            // Bind each perf fd into the corresponding PERF_EVENT_ARRAY map slot.
            if ((ioctl(perfFd, PERF_EVENT_IOC_RESET, 0) != 0) ||
                (ioctl(perfFd, PERF_EVENT_IOC_ENABLE, 0) != 0) ||
                (bpf_map_update_elem(localPerfMapFds[eventSpecIdx], &cpuIdx, &perfFd, BPF_ANY) != 0)) {
                closePerfFds(localPerfFds);
                return -errno;
            }
        }
    }

    // Attach tracepoint program that reads perf counters in-kernel.
    bpf_program* pProgram = bpf_object__find_program_by_name(objectPtr.get(), kProgramName.data());
    if (!pProgram) {
        closePerfFds(localPerfFds);
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
        closePerfFds(localPerfFds);
        return err;
    }

    _bpfObjectPtr = std::move(objectPtr);
    _bpfLinkPtr = std::move(linkPtr);
    _perfMapFds = localPerfMapFds;
    _perfFds = std::move(localPerfFds);
    _isInitialized = true;

    return 0;
}

int EBpfCacheProfiler::configureTargetPid(pid_t targetPid) {
    uint32_t targetPidMapKey = 0;
    uint32_t targetPidValue = static_cast<uint32_t>(targetPid);

    // Tell eBPF which process should be sampled in this interval.
    if (bpf_map_update_elem(_targetPidMapFd, &targetPidMapKey, &targetPidValue, BPF_ANY) != 0) {
        return -errno;
    }

    return 0;
}

int EBpfCacheProfiler::resetPerfCounters() {
    // Reset per-CPU perf counters so each interval reports fresh deltas.
    for (int perfFd : _perfFds) {
        if ((ioctl(perfFd, PERF_EVENT_IOC_DISABLE, 0) != 0) ||
            (ioctl(perfFd, PERF_EVENT_IOC_RESET, 0) != 0) ||
            (ioctl(perfFd, PERF_EVENT_IOC_ENABLE, 0) != 0)) {
            return -errno;
        }
    }

    return 0;
}

int EBpfCacheProfiler::resetTotals() {
    // Clear per-CPU totals map to avoid carrying values across intervals.
    std::vector<uint64_t> perCpuZeros(static_cast<size_t>(_cpuCount), 0);
    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        uint32_t totalsMapKey = static_cast<uint32_t>(kPerfEventSpecs[eventSpecIdx].eventIdx);
        if (bpf_map_update_elem(_totalsMapFd, &totalsMapKey, perCpuZeros.data(), BPF_ANY) != 0) {
            return -errno;
        }
    }

    return 0;
}

int EBpfCacheProfiler::readTotals(CacheSample& rSampleOutput) {
    // Initialize output so caller never sees stale values.
    rSampleOutput = {0, 0, 0, 0, 0, 0};

    // Read each per-CPU event bucket and aggregate to one sample snapshot.
    int err = 0;
    err = sumTotalsForEvent(_totalsMapFd, CacheEventIdx::kL1ReadAccess, _cpuCount, rSampleOutput.l1ReadAccesses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(_totalsMapFd, CacheEventIdx::kL1ReadMiss, _cpuCount, rSampleOutput.l1ReadMisses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(_totalsMapFd, CacheEventIdx::kL2ReadAccess, _cpuCount, rSampleOutput.l2ReadAccesses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(_totalsMapFd, CacheEventIdx::kL2ReadMiss, _cpuCount, rSampleOutput.l2ReadMisses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(_totalsMapFd, CacheEventIdx::kLlcReadAccess, _cpuCount, rSampleOutput.llcReadAccesses);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(_totalsMapFd, CacheEventIdx::kLlcReadMiss, _cpuCount, rSampleOutput.llcReadMisses);
    if (err != 0) {
        return err;
    }

    return 0;
}
