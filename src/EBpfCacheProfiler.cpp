#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "EBpfCacheProfiler.h"
#include "Logger.h"

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

int openPerfEventFd(
    pid_t targetPid,
    int cpuIdx,
    perf_type_id perfType,
    uint64_t config,
    uint64_t config1,
    uint64_t config2) {
    struct perf_event_attr attr = {};

    // Configure one hardware cache counter for one CPU while scoping counting
    // to the target PID context on that CPU.
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

    return static_cast<int>(syscall(SYS_perf_event_open, &attr, targetPid, cpuIdx, -1, 0));
}

int readPerfCounterValue(int perfFd, uint64_t& rOutput) {
    rOutput = 0;
    ssize_t byteCount = read(perfFd, &rOutput, sizeof(rOutput));
    if (byteCount < 0) {
        return -errno;
    }
    if (byteCount != static_cast<ssize_t>(sizeof(rOutput))) {
        return -EIO;
    }

    return 0;
}

int sumPerfCountersForEvent(const std::vector<int>& rPerfFds, int cpuCount, size_t eventSpecIdx, uint64_t& rOutput) {
    rOutput = 0;

    const size_t eventOffset = eventSpecIdx * static_cast<size_t>(cpuCount);
    const size_t requiredPerfFdCount = static_cast<size_t>(cpuCount) * kPerfEventSpecs.size();
    if (rPerfFds.size() < requiredPerfFdCount) {
        return -EINVAL;
    }

    for (int cpuIdx = 0; cpuIdx < cpuCount; ++cpuIdx) {
        uint64_t perfCounterValue = 0;
        int err = readPerfCounterValue(
            rPerfFds[eventOffset + static_cast<size_t>(cpuIdx)],
            perfCounterValue);
        if (err != 0) {
            return err;
        }

        rOutput += perfCounterValue;
    }

    return 0;
}

void closePerfFds(std::vector<int>& rPerfFds) {
    for (int perfFd : rPerfFds) {
        close(perfFd);
    }
    rPerfFds.clear();
}

}  // namespace

EBpfCacheProfiler::EBpfCacheProfiler(const std::string& rBpfObjectPath)
    : _bpfObjectPath(rBpfObjectPath),
      _bpfObjectPtr(nullptr, &bpf_object__close),
      _bpfLinkPtr(nullptr, &bpf_link__destroy),
      _perfMapFds({-1, -1, -1, -1, -1, -1}),
      _targetPidMapFd(-1),
      _cpuCount(0),
      _isProfilerInitialized(false) {
}

EBpfCacheProfiler::~EBpfCacheProfiler() {
    closePerfFds(_perfFds);
}

int EBpfCacheProfiler::initializeProfiling(pid_t targetPid) {
    if (_isProfilerInitialized) {
        return 0;
    }

    int resourcesStatus = initializeProfilerResources(targetPid);
    if (resourcesStatus != 0) {
        return resourcesStatus;
    }

    // Target PID map is configured during session initialization.
    int err = configureTargetPid(targetPid);
    if (err != 0) {
        return err;
    }
    _isProfilerInitialized = true;

    return 0;
}

int EBpfCacheProfiler::sampleOnce(uint32_t sampleIntervalMs, CacheSample& rSampleOutput) {
    if (!_isProfilerInitialized) {
        return -EINVAL;
    }

    // Cumulative counting: each sample reports totals since profiler initialization
    usleep(sampleIntervalMs * 1000U);

    int err = readTotals(rSampleOutput);
    if (err != 0) {
        Logger::error("Failed to read cumulative totals: status=" + std::to_string(err));
        return err;
    }

    return 0;
}

int EBpfCacheProfiler::initializeProfilerResources(pid_t targetPid) {
    // Discover the number of online CPUs for this profiling session.
    // This value determines how many per-CPU perf FDs are created per event.
    long cpuCountLong = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpuCountLong <= 0) {
        Logger::error("Failed to determine online CPU count via sysconf(_SC_NPROCESSORS_ONLN)");
        return -EINVAL;
    }
    _cpuCount = static_cast<int>(cpuCountLong);
    Logger::debug("Detected online CPU count: " + std::to_string(_cpuCount));

    // Open the compiled BPF ELF object. This parses maps/programs in user space,
    // but does not yet load anything into kernel memory.
    Logger::debug("Opening BPF object: " + _bpfObjectPath);
    std::unique_ptr<bpf_object, decltype(&bpf_object__close)> objectPtr(bpf_object__open_file(_bpfObjectPath.c_str(), nullptr), &bpf_object__close);
    int err = static_cast<int>(libbpf_get_error(objectPtr.get()));
    if (err != 0) {
        Logger::error("Failed to open BPF object: status=" + std::to_string(err));
        objectPtr.release();
        return err;
    }

    // Load maps/programs into the kernel so map FDs can be resolved and program
    // attach can proceed.
    err = bpf_object__load(objectPtr.get());
    if (err != 0) {
        Logger::error("Failed to load BPF object into kernel: status=" + std::to_string(err));
        return err;
    }

    // Resolve mandatory target map so userspace can configure PID filtering for
    // the BPF program.
    _targetPidMapFd = bpf_object__find_map_fd_by_name(objectPtr.get(), kTargetPidMapName.data());
    if (_targetPidMapFd < 0) {
        Logger::error("Failed to resolve required map: target_pid");
        return -ENOENT;
    }

    // Resolve all PERF_EVENT_ARRAY maps up front. Each map corresponds to one
    // tracked cache event and will be filled with one perf FD per CPU.
    std::array<int, kPerfEventSpecs.size()> localPerfMapFds = {};
    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        localPerfMapFds[eventSpecIdx] = bpf_object__find_map_fd_by_name(objectPtr.get(), kPerfEventSpecs[eventSpecIdx].mapName.data());
        if (localPerfMapFds[eventSpecIdx] < 0) {
            Logger::error("Failed to resolve PERF_EVENT_ARRAY map: " + std::string(kPerfEventSpecs[eventSpecIdx].mapName));
            return -ENOENT;
        }
    }

    // Keep an owning list of all opened FDs so failure cleanup can close
    // everything from one place no matter where initialization fails.
    std::vector<int> localPerfFds;
    localPerfFds.reserve(static_cast<size_t>(_cpuCount * kPerfEventSpecs.size()));

    for (size_t eventSpecIdx = 0; eventSpecIdx < kPerfEventSpecs.size(); ++eventSpecIdx) {
        // Store CPU-ordered FDs for this event to bind by CPU index.
        std::vector<int> eventFds;
        eventFds.reserve(static_cast<size_t>(_cpuCount));
        Logger::debug("Opening per-CPU perf events for map: " + std::string(kPerfEventSpecs[eventSpecIdx].mapName));

        // Open one perf counter FD per online CPU for this event, scoped to
        // the target PID context.
        for (int cpuIdx = 0; cpuIdx < _cpuCount; ++cpuIdx) {
            int perfFd = openPerfEventFd(
                targetPid,
                cpuIdx,
                kPerfEventSpecs[eventSpecIdx].perfType,
                kPerfEventSpecs[eventSpecIdx].config,
                kPerfEventSpecs[eventSpecIdx].config1,
                kPerfEventSpecs[eventSpecIdx].config2);
            if (perfFd < 0) {
                Logger::error("perf_event_open failed for map=" + std::string(kPerfEventSpecs[eventSpecIdx].mapName) + " cpu=" + std::to_string(cpuIdx));
                closePerfFds(localPerfFds);
                return -errno;
            }
            eventFds.push_back(perfFd);
            localPerfFds.push_back(perfFd);
        }

        // Prime each FD and bind it into the map entry keyed by cpuIdx so the BPF
        // program can read the corresponding CPU-local counter.
        for (int cpuIdx = 0; cpuIdx < _cpuCount; ++cpuIdx) {
            int perfFd = eventFds[cpuIdx];
            if ((ioctl(perfFd, PERF_EVENT_IOC_RESET, 0) != 0) ||
                (ioctl(perfFd, PERF_EVENT_IOC_ENABLE, 0) != 0) ||
                (bpf_map_update_elem(localPerfMapFds[eventSpecIdx], &cpuIdx, &perfFd, BPF_ANY) != 0)) {
                Logger::error("Failed to reset/enable/bind perf FD for map=" + std::string(kPerfEventSpecs[eventSpecIdx].mapName) + " cpu=" + std::to_string(cpuIdx));
                closePerfFds(localPerfFds);
                return -errno;
            }
        }
    }

    // Resolve and attach the BPF sampling program.
    bpf_program* pProgram = bpf_object__find_program_by_name(objectPtr.get(), kProgramName.data());
    if (!pProgram) {
        Logger::error("Failed to find BPF program: " + std::string(kProgramName));
        closePerfFds(localPerfFds);
        return -ENOENT;
    }

    std::unique_ptr<bpf_link, decltype(&bpf_link__destroy)> linkPtr(bpf_program__attach(pProgram), &bpf_link__destroy);
    err = static_cast<int>(libbpf_get_error(linkPtr.get()));
    if (err != 0) {
        Logger::error("Failed to attach BPF program: status=" + std::to_string(err));
        linkPtr.release();
        closePerfFds(localPerfFds);
        return err;
    }

    // Commit fully initialized resources to object state only after all setup
    // steps succeed. This keeps partial init state out of the class members.
    _bpfObjectPtr = std::move(objectPtr);
    _bpfLinkPtr = std::move(linkPtr);
    _perfMapFds = localPerfMapFds;
    _perfFds = std::move(localPerfFds);
    Logger::info(
        "Profiler resources initialized: target_pid=" + std::to_string(static_cast<int>(targetPid)) +
        " cpus=" + std::to_string(_cpuCount) +
        " perf_fds=" + std::to_string(_perfFds.size()));
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

int EBpfCacheProfiler::readTotals(CacheSample& rSampleOutput) {
    // Initialize output so caller never sees stale values.
    rSampleOutput = {0, 0, 0, 0, 0, 0};

    // Read one kernel perf FD per CPU and event, then aggregate to one
    // cumulative snapshot.
    int err = 0;
    err = sumPerfCountersForEvent(
        _perfFds,
        _cpuCount,
        static_cast<size_t>(CacheEventIdx::kL1ReadAccess),
        rSampleOutput.l1ReadAccessTotal);
    if (err != 0) {
        return err;
    }
    err = sumPerfCountersForEvent(
        _perfFds,
        _cpuCount,
        static_cast<size_t>(CacheEventIdx::kL1ReadMiss),
        rSampleOutput.l1ReadMissTotal);
    if (err != 0) {
        return err;
    }
    err = sumPerfCountersForEvent(
        _perfFds,
        _cpuCount,
        static_cast<size_t>(CacheEventIdx::kL2ReadAccess),
        rSampleOutput.l2ReadAccessTotal);
    if (err != 0) {
        return err;
    }
    err = sumPerfCountersForEvent(
        _perfFds,
        _cpuCount,
        static_cast<size_t>(CacheEventIdx::kL2ReadMiss),
        rSampleOutput.l2ReadMissTotal);
    if (err != 0) {
        return err;
    }
    err = sumPerfCountersForEvent(
        _perfFds,
        _cpuCount,
        static_cast<size_t>(CacheEventIdx::kLlcReadAccess),
        rSampleOutput.llcReadAccessTotal);
    if (err != 0) {
        return err;
    }
    err = sumPerfCountersForEvent(
        _perfFds,
        _cpuCount,
        static_cast<size_t>(CacheEventIdx::kLlcReadMiss),
        rSampleOutput.llcReadMissTotal);
    if (err != 0) {
        return err;
    }

    return 0;
}
