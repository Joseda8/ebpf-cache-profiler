#ifndef ICACHESAMPLELOGGER_H
#define ICACHESAMPLELOGGER_H

#include <sys/types.h>

#include <stdint.h>

#include "CacheSample.h"

/**
 * @brief Interface for consuming sampled cache metrics.
 */
class ICacheSampleLogger {
public:
    virtual ~ICacheSampleLogger() = default;

    /**
     * @brief Consumes one cache sample.
     *
     * @param sampleIdx Sequential sample index.
     * @param elapsedMs Elapsed time since profiling start.
     * @param targetPid Profiled process identifier.
     * @param rSample Cumulative sample payload since profiling start.
     */
    virtual void logSample(uint64_t sampleIdx, uint64_t elapsedMs, pid_t targetPid, const CacheSample& rSample) = 0;

    /**
     * @brief Notifies logger that the profiled process exited.
     *
     * @param targetPid Profiled process identifier.
     */
    virtual void logTargetExit(pid_t targetPid) = 0;
};

#endif
