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

// Raw PMU encodings validated on this target machine with `perf list --details`.
const std::array<EBpfCacheProfiler::PerfEventSpec, 6> EBpfCacheProfiler::kPerfEventSpecs = {{
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
     kL2RqstsExtraFilterConfig1,
     0,
     CacheEventIdx::kL2ReadAccess},
    {"l2_read_miss_events",
     PERF_TYPE_RAW,
     kL2RqstsMissesConfig,
     kL2RqstsExtraFilterConfig1,
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

namespace {

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

int sumTotalsForEvent(int totalsMapFd, uint32_t mapKey, int cpuCount, uint64_t& rOutput) {
    // Always return a fresh aggregate value for this event.
    rOutput = 0;

    // PERCPU_ARRAY lookups return one value per online CPU for the given key.
    // Allocate a userspace buffer large enough to receive all CPU slots.
    std::vector<uint64_t> perCpuValues(static_cast<size_t>(cpuCount), 0);
    // Read all per-CPU counters for this event from cache_totals[eventIdx].
    if (bpf_map_lookup_elem(totalsMapFd, &mapKey, perCpuValues.data()) != 0) {
        return -errno;
    }

    // Collapse per-CPU values into one process-wide total for reporting.
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

}  // namespace

EBpfCacheProfiler::EBpfCacheProfiler(const std::string& rBpfObjectPath)
    : _bpfObjectPath(rBpfObjectPath),
      _bpfObjectPtr(nullptr, &bpf_object__close),
      _bpfLinkPtr(nullptr, &bpf_link__destroy),
      _perfMapFds({-1, -1, -1, -1, -1, -1}),
      _targetPidMapFd(-1),
      _totalsMapFd(-1),
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

    int resourcesStatus = initializeProfilerResources();
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

int EBpfCacheProfiler::initializeProfilerResources() {
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

    // Resolve mandatory maps:
    // - target_pid: userspace writes PID filter for the BPF program
    // - cache_totals: userspace reads per-CPU accumulated values each interval
    _targetPidMapFd = bpf_object__find_map_fd_by_name(objectPtr.get(), kTargetPidMapName.data());
    _totalsMapFd = bpf_object__find_map_fd_by_name(objectPtr.get(), kTotalsMapName.data());
    if ((_targetPidMapFd < 0) || (_totalsMapFd < 0)) {
        Logger::error("Failed to resolve required maps: target_pid and/or cache_totals");
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

        // Open one perf counter FD per online CPU for this event.
        for (int cpuIdx = 0; cpuIdx < _cpuCount; ++cpuIdx) {
            int perfFd = openPerfEventFd(
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

        // Init each FD and bind it into the map entry keyed by cpuIdx so the BPF
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
        "Profiler resources initialized: cpus=" + std::to_string(_cpuCount) +
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

    // Read each per-CPU event bucket accumulated in-kernel
    int err = 0;
    err = sumTotalsForEvent(
        _totalsMapFd,
        static_cast<uint32_t>(CacheEventIdx::kL1ReadAccess),
        _cpuCount,
        rSampleOutput.l1ReadAccessTotal);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(
        _totalsMapFd,
        static_cast<uint32_t>(CacheEventIdx::kL1ReadMiss),
        _cpuCount,
        rSampleOutput.l1ReadMissTotal);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(
        _totalsMapFd,
        static_cast<uint32_t>(CacheEventIdx::kL2ReadAccess),
        _cpuCount,
        rSampleOutput.l2ReadAccessTotal);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(
        _totalsMapFd,
        static_cast<uint32_t>(CacheEventIdx::kL2ReadMiss),
        _cpuCount,
        rSampleOutput.l2ReadMissTotal);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(
        _totalsMapFd,
        static_cast<uint32_t>(CacheEventIdx::kLlcReadAccess),
        _cpuCount,
        rSampleOutput.llcReadAccessTotal);
    if (err != 0) {
        return err;
    }
    err = sumTotalsForEvent(
        _totalsMapFd,
        static_cast<uint32_t>(CacheEventIdx::kLlcReadMiss),
        _cpuCount,
        rSampleOutput.llcReadMissTotal);
    if (err != 0) {
        return err;
    }

    return 0;
}
