#ifndef CACHEPROFILERAPP_H
#define CACHEPROFILERAPP_H

#include "CacheSampleLoggerConfig.h"
#include "CsvCacheSampleLogger.h"
#include "ICacheProfiler.h"
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
     * @param rLoggerConfig Logger backend configuration.
     */
    explicit CacheProfilerApp(const CacheSampleLoggerConfig& rLoggerConfig);

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

    CacheSampleLoggerConfig _loggerConfig;
    std::unique_ptr<ICacheProfiler> _profilerPtr;
    std::unique_ptr<CsvCacheSampleLogger> _csvLoggerPtr;
    int _loggerSetupStatus;
};

#endif
