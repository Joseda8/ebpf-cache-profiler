#ifndef EBPFCACHEPROFILER_H
#define EBPFCACHEPROFILER_H

#include "ICacheProfiler.h"

#include <linux/perf_event.h>

#include <bpf/libbpf.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief eBPF-backed cache profiler implementation.
 */
class EBpfCacheProfiler : public ICacheProfiler {
public:
    /**
     * @brief Creates a profiler that loads the given eBPF object file.
     *
     * @param rBpfObjectPath Path to the compiled eBPF object.
     */
    explicit EBpfCacheProfiler(const std::string& rBpfObjectPath);
    ~EBpfCacheProfiler() override;

    int initializeProfiling(pid_t targetPid) override;
    int sampleOnce(uint32_t sampleIntervalMs, CacheSample& rSampleOutput) override;

private:
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

    static constexpr std::string_view kProgramName = "sampleOnSchedSwitch";
    static constexpr std::string_view kTargetPidMapName = "target_pid";
    static constexpr std::string_view kTotalsMapName = "cache_totals";

    // Alias/event metadata is taken from `perf list --details`.
    // Raw PMU config constants below follow: config = event | (umask << 8).

    // perf list --details: l2_rqsts.references (event=0x24, umask=0xff).
    static constexpr uint64_t kL2RqstsReferencesConfig = 0xFF24;
    // perf list --details: l2_rqsts.miss (event=0x24, umask=0x3f).
    static constexpr uint64_t kL2RqstsMissesConfig = 0x3F24;
    // perf list --details: l2_rqsts.{references,miss} extra filter field.
    // config1 is taken directly from the alias details.
    static constexpr uint64_t kL2RqstsExtraFilterConfig1 = 0x30D43;
    // perf list --details: longest_lat_cache.reference (event=0x2e, umask=0x4f).
    static constexpr uint64_t kLongestLatCacheReferenceConfig = 0x4F2E;
    // perf list --details: longest_lat_cache.miss (event=0x2e, umask=0x41).
    static constexpr uint64_t kLongestLatCacheMissConfig = 0x412E;
    // perf list --details: longest_lat_cache.{reference,miss} extra filter field.
    // config1 is taken directly from the alias details.
    static constexpr uint64_t kLongestLatCacheExtraFilterConfig1 = 0x186A3;

    static const std::array<PerfEventSpec, 6> kPerfEventSpecs;

    int initializeProfilerResources();
    int configureTargetPid(pid_t targetPid);
    int readTotals(CacheSample& rSampleOutput);

    std::string _bpfObjectPath;
    std::unique_ptr<bpf_object, decltype(&bpf_object__close)> _bpfObjectPtr;
    std::unique_ptr<bpf_link, decltype(&bpf_link__destroy)> _bpfLinkPtr;
    std::array<int, 6> _perfMapFds;
    std::vector<int> _perfFds;
    int _targetPidMapFd;
    int _totalsMapFd;
    int _cpuCount;
    bool _isProfilerInitialized;
};

#endif
