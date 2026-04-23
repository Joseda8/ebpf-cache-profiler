#ifndef ICACHEPROFILER_H
#define ICACHEPROFILER_H

#include <sys/types.h>

#include <stdint.h>

#include "CacheSample.h"

/**
 * @brief Interface for cache profilers that sample process-level counters.
 */
class ICacheProfiler {
public:
    virtual ~ICacheProfiler() = default;

    /**
     * @brief Samples cache counters once for one PID.
     *
     * @param targetPid Target process ID to sample.
     * @param sampleIntervalMs Sampling interval in milliseconds.
     * @param rSampleOutput Output sample structure.
     *
     * @return Status code from the operation.
     * @retval 0 Success.
     * @retval Negative errno code Failure.
     */
    virtual int sampleOnce(pid_t targetPid, uint32_t sampleIntervalMs, CacheSample& rSampleOutput) = 0;
};

#endif
