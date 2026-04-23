#include "CacheProfilerApp.h"

#include <errno.h>
#include <signal.h>

#include <chrono>
namespace {

volatile sig_atomic_t gStopRequested = 0;

void onStopSignal(int signalNumber) {
    // Unused signal id.
    (void)signalNumber;
    gStopRequested = 1;
}

}  // namespace

CacheProfilerApp::CacheProfilerApp(std::unique_ptr<ICacheProfiler> profilerPtrIn, std::unique_ptr<ICacheSampleLogger> loggerPtrIn)
    : _profilerPtr(std::move(profilerPtrIn)), _loggerPtr(std::move(loggerPtrIn)) {}

int CacheProfilerApp::run(const ProfilingConfig& rConfig) {
    if (!_profilerPtr || !_loggerPtr) {
        return -EINVAL;
    }

    signal(SIGINT, onStopSignal);
    signal(SIGTERM, onStopSignal);

    auto startTime = std::chrono::steady_clock::now();
    uint64_t sampleIdx = 0;

    while (gStopRequested == 0) {
        if (!isProcessAlive(rConfig.targetPid)) {
            _loggerPtr->logTargetExit(rConfig.targetPid);
            return 0;
        }

        CacheSample sample = {0, 0, 0, 0, 0, 0};
        int rc = _profilerPtr->sampleOnce(rConfig.targetPid, rConfig.sampleIntervalMs, sample);
        if (rc != 0) {
            return rc;
        }

        auto nowTime = std::chrono::steady_clock::now();
        uint64_t elapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - startTime).count());

        _loggerPtr->logSample(sampleIdx, elapsedMs, rConfig.targetPid, sample);

        ++sampleIdx;

        if (rConfig.hasDurationLimit && (elapsedMs >= rConfig.profileDurationMs)) {
            return 0;
        }
    }

    return 0;
}

bool CacheProfilerApp::isProcessAlive(pid_t targetPid) const {
    if (kill(targetPid, 0) == 0) {
        return true;
    }

    return (errno != ESRCH);
}
