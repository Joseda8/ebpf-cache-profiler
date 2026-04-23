#ifndef CACHEPROFILERAPP_H
#define CACHEPROFILERAPP_H

#include "ICacheProfiler.h"
#include "ICacheSampleLogger.h"
#include "ProfilingConfig.h"

#include <memory>

/**
 * @brief Runs periodic cache sampling.
 */
class CacheProfilerApp {
public:
    /**
     * @brief Creates an app that uses the built-in eBPF profiler and logger mode selection.
     *
     * @param terminalLogEnabled Enables terminal logging when true.
     */
    explicit CacheProfilerApp(bool terminalLogEnabled);

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
     * @brief Creates an app with explicit profiler and logger dependencies.
     *
     * @param profilerPtr Profiler implementation ownership.
     * @param loggerPtr Logger implementation ownership.
     */
    explicit CacheProfilerApp(std::unique_ptr<ICacheProfiler> profilerPtr, std::unique_ptr<ICacheSampleLogger> loggerPtr);

    /**
     * @brief Checks whether a PID currently exists.
     *
     * @param targetPid PID to check.
     * @return True when PID exists or is not probeable due to permissions.
     */
    bool isProcessAlive(pid_t targetPid) const;

    std::unique_ptr<ICacheProfiler> _profilerPtr;
    std::unique_ptr<ICacheSampleLogger> _loggerPtr;
};

#endif
