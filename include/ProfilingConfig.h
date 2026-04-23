#ifndef PROFILINGCONFIG_H
#define PROFILINGCONFIG_H

#include <sys/types.h>

#include <stdint.h>

/**
 * @brief Runtime configuration for cache profiling.
 */
struct ProfilingConfig {
    // PID that the profiler will monitor.
    pid_t targetPid;

    // Length of each sampling window in milliseconds.
    uint32_t sampleIntervalMs;

    // Enables finite profiling when true; otherwise runs until stopped/target exit.
    bool hasDurationLimit;

    // Total profiling duration in milliseconds when duration limit is enabled.
    uint64_t profileDurationMs;
};

#endif
