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
    std::string_view mapName;
    perf_type_id perfType;
    uint64_t config;
    uint64_t config1;
    uint64_t config2;
    CacheEventIdx eventIdx;
};

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
    : bpfObjectPath(rBpfObjectPath),
      bpfObjectPtr(nullptr, &bpf_object__close),
      bpfLinkPtr(nullptr, &bpf_link__destroy),
      perfMapFds({-1, -1, -1, -1, -1, -1}),
      targetPidMapFd(-1),
      totalsMapFd(-1),
      cpuCount(0),
      isInitialized(false) {}

EBpfCacheProfiler::~EBpfCacheProfiler() {
    closePerfFds(perfFds);
}

int EBpfCacheProfiler::sampleOnce(pid_t targetPid, uint32_t sampleIntervalMs, CacheSample& rSampleOutput) {
    int err = initializeOnce();
    if (err != 0) {
        return err;
    }

    err = configureTargetPid(targetPid);
    if (err != 0) {
        return err;
    }

    err = resetPerfCounters();
    if (err != 0) {
        return err;
    }

    err = resetTotals();
    if (err != 0) {
        return err;
    }

    // Sampling interval.
    usleep(sampleIntervalMs * 1000U);

    return readTotals(rSampleOutput);
}

int EBpfCacheProfiler::initializeOnce() {
    if (isInitialized) {
        return 0;
    }

    // Install log filtering for the known BTF-map fallback noise.
    // This does not disable eBPF sampling; it only suppresses that startup line.
    libbpf_set_print(libbpfLogCallback);

    // We create one perf counter per CPU per tracked event.
    long cpuCountLong = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpuCountLong <= 0) {
        return -EINVAL;
    }
    cpuCount = static_cast<int>(cpuCountLong);

    // Load maps/programs from the compiled eBPF object.
    auto objectPtr = std::unique_ptr<bpf_object, decltype(&bpf_object__close)>(
        bpf_object__open_file(bpfObjectPath.c_str(), nullptr), &bpf_object__close);
    int err = static_cast<int>(libbpf_get_error(objectPtr.get()));
    if (err != 0) {
        objectPtr.release();
        return err;
    }

    err = bpf_object__load(objectPtr.get());
    if (err != 0) {
        return err;
    }

    // Resolve required maps used by user-space setup and readback.
    targetPidMapFd = bpf_object__find_map_fd_by_name(objectPtr.get(), kTargetPidMapName.data());
    totalsMapFd = bpf_object__find_map_fd_by_name(objectPtr.get(), kTotalsMapName.data());
    if ((targetPidMapFd < 0) || (totalsMapFd < 0)) {
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
    localPerfFds.reserve(static_cast<size_t>(cpuCount * kPerfEventSpecs.size()));

    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        std::vector<int> eventFds;
        eventFds.reserve(static_cast<size_t>(cpuCount));

        // Open one perf counter fd per CPU for this event type.
        for (int cpuIdx = 0; cpuIdx < cpuCount; ++cpuIdx) {
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

        for (int cpuIdx = 0; cpuIdx < cpuCount; ++cpuIdx) {
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

    bpfObjectPtr = std::move(objectPtr);
    bpfLinkPtr = std::move(linkPtr);
    perfMapFds = localPerfMapFds;
    perfFds = std::move(localPerfFds);
    isInitialized = true;

    return 0;
}

int EBpfCacheProfiler::configureTargetPid(pid_t targetPid) {
    uint32_t targetPidMapKey = 0;
    uint32_t targetPidValue = static_cast<uint32_t>(targetPid);

    // Tell eBPF which PID should be sampled.
    if (bpf_map_update_elem(targetPidMapFd, &targetPidMapKey, &targetPidValue, BPF_ANY) != 0) {
        return -errno;
    }

    return 0;
}

int EBpfCacheProfiler::resetPerfCounters() {
    for (int perfFd : perfFds) {
        if ((ioctl(perfFd, PERF_EVENT_IOC_DISABLE, 0) != 0) ||
            (ioctl(perfFd, PERF_EVENT_IOC_RESET, 0) != 0) ||
            (ioctl(perfFd, PERF_EVENT_IOC_ENABLE, 0) != 0)) {
            return -errno;
        }
    }

    return 0;
}

int EBpfCacheProfiler::resetTotals() {
    std::vector<uint64_t> perCpuZeros(static_cast<size_t>(cpuCount), 0);
    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        uint32_t totalsMapKey = static_cast<uint32_t>(kPerfEventSpecs[eventSpecIdx].eventIdx);
        if (bpf_map_update_elem(totalsMapFd, &totalsMapKey, perCpuZeros.data(), BPF_ANY) != 0) {
            return -errno;
        }
    }

    return 0;
}

int EBpfCacheProfiler::readTotals(CacheSample& rSampleOutput) {
    // Initialize output so caller never sees stale values.
    rSampleOutput = {0, 0, 0, 0, 0, 0};

    int err = 0;
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

    return 0;
}
