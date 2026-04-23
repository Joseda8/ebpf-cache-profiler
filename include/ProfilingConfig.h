#ifndef PROFILINGCONFIG_H
#define PROFILINGCONFIG_H

#include <sys/types.h>

#include <stdint.h>

#include <string>

/**
 * @brief Runtime configuration for cache profiling.
 */
struct ProfilingConfig {
    pid_t targetPid;
    uint32_t sampleIntervalMs;
    std::string bpfObjectPath;
    bool hasDurationLimit;
    uint64_t profileDurationMs;
};

#endif
