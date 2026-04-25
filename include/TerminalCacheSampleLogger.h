#ifndef TERMINALCACHESAMPLELOGGER_H
#define TERMINALCACHESAMPLELOGGER_H

#include "ICacheSampleLogger.h"

/**
 * @brief Terminal logger for cumulative cache samples.
 */
class TerminalCacheSampleLogger : public ICacheSampleLogger {
public:
    void logSample(uint64_t sampleIdx, uint64_t elapsedMs, pid_t targetPid, const CacheSample& rSample) override;
    void logTargetExit(pid_t targetPid) override;
};

#endif
