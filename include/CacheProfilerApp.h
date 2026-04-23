#ifndef CACHEPROFILERAPP_H
#define CACHEPROFILERAPP_H

#include "ICacheProfiler.h"
#include "ProfilingConfig.h"

#include <memory>

/**
 * @brief Runs periodic cache sampling and emits terminal output.
 */
class CacheProfilerApp {
public:
    /**
     * @brief Creates an app with one profiler implementation.
     *
     * @param profilerPtr Profiler implementation ownership.
     */
    explicit CacheProfilerApp(std::unique_ptr<ICacheProfiler> profilerPtr);

    /**
     * @brief Runs profiling according to runtime configuration.
     *
     * @param rConfig Runtime profiling options.
     *
     * @return Status code from the operation.
     * @retval 0 Success.
     * @retval Negative errno code Failure.
     */
    int run(const ProfilingConfig& rConfig);

private:
    /**
     * @brief Checks whether a PID currently exists.
     *
     * @param targetPid PID to check.
     * @return True when PID exists or is not probeable due to permissions.
     */
    bool isProcessAlive(pid_t targetPid) const;

    std::unique_ptr<ICacheProfiler> _profilerPtr;
};

#endif
