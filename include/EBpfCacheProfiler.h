#ifndef EBPFCACHEPROFILER_H
#define EBPFCACHEPROFILER_H

#include "ICacheProfiler.h"

#include <string>

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

    int sampleOnce(pid_t targetPid, uint32_t sampleIntervalMs, CacheSample& rSampleOutput) override;

private:
    std::string bpfObjectPath;
};

#endif
