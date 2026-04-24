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
     * @brief Initializes profiler session state for a target PID.
     *
     * @param targetPid Target process ID to sample.
     *
     * @return Status code from the operation.
     * @retval 0 Success.
     * @retval Negative errno code Failure.
     */
    virtual int initializeProfiling(pid_t targetPid) = 0;

    /**
     * @brief Samples cache counters once for the configured target PID.
     *
     * @param sampleIntervalMs Sampling interval in milliseconds.
     * @param rSampleOutput Output sample structure.
     *
     * @return Status code from the operation.
     * @retval 0 Success.
     * @retval Negative errno code Failure.
     */
    virtual int sampleOnce(uint32_t sampleIntervalMs, CacheSample& rSampleOutput) = 0;
};

#endif
