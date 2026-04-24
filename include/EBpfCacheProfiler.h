#ifndef EBPFCACHEPROFILER_H
#define EBPFCACHEPROFILER_H

#include "ICacheProfiler.h"

#include <bpf/libbpf.h>

#include <array>
#include <memory>
#include <string>
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
    int initializeProfilerResources();
    int configureTargetPid(pid_t targetPid);
    int resetPerfCounters();
    int resetTotals();
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
