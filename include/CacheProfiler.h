#ifndef CACHEPROFILER_H
#define CACHEPROFILER_H

#include <stdint.h>
#include <sys/types.h>

#include <string>

/**
 * @brief Snapshot of L1 read counters for a target PID.
 */
struct CacheSample {
    uint64_t l1ReadAccesses;
    uint64_t l1ReadMisses;
};

/**
 * @brief Minimal cache profiler facade for L1 read events.
 */
class CacheProfiler {
public:
    /**
     * @brief Samples L1 counters once.
     *
     * @param targetPid Target process ID to sample.
     * @param sampleIntervalMs Sampling interval in milliseconds.
     * @param rBpfObjectPath Path to the compiled eBPF object file.
     * @param rSampleOutput Output sample.
     *
     * @return Status code from the operation.
     * @retval 0 Success.
     * @retval Negative errno code Failure.
     */
    static int sampleOnce(
        pid_t targetPid, uint32_t sampleIntervalMs,
        const std::string& rBpfObjectPath, CacheSample& rSampleOutput);
};

#endif
