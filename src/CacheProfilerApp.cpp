#include "CacheProfilerApp.h"
#include "EBpfCacheProfiler.h"
#include "TerminalCacheSampleLogger.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <string>

namespace {

volatile sig_atomic_t gStopRequested = 0;

void onStopSignal(int signalNumber) {
    // Unused signal id.
    (void)signalNumber;
    gStopRequested = 1;
}

std::unique_ptr<ICacheSampleLogger> createLogger(bool terminalLogEnabled) {
    if (terminalLogEnabled) {
        return std::make_unique<TerminalCacheSampleLogger>();
    }

    return nullptr;
}

std::string resolveDefaultBpfObjectPath() {
    std::array<char, PATH_MAX> executablePathBuffer = {};
    ssize_t byteCount = readlink("/proc/self/exe", executablePathBuffer.data(), executablePathBuffer.size() - 1);
    if (byteCount <= 0) {
        return "cache_sampler.bpf.o";
    }

    executablePathBuffer[static_cast<size_t>(byteCount)] = '\0';
    std::string executablePath(executablePathBuffer.data());
    std::string::size_type slashIdx = executablePath.find_last_of('/');
    if (slashIdx == std::string::npos) {
        return "cache_sampler.bpf.o";
    }

    return executablePath.substr(0, slashIdx + 1) + "cache_sampler.bpf.o";
}

}  // namespace

CacheProfilerApp::CacheProfilerApp(bool terminalLogEnabled)
    : CacheProfilerApp(std::make_unique<EBpfCacheProfiler>(resolveDefaultBpfObjectPath()), createLogger(terminalLogEnabled)) {}

CacheProfilerApp::CacheProfilerApp(std::unique_ptr<ICacheProfiler> profilerPtrIn, std::unique_ptr<ICacheSampleLogger> loggerPtrIn)
    : _profilerPtr(std::move(profilerPtrIn)), _loggerPtr(std::move(loggerPtrIn)) {}

int CacheProfilerApp::run(const ProfilingConfig& rConfig) {
    if (!_profilerPtr) {
        return -EINVAL;
    }
    if (!_loggerPtr) {
        return -ENOSYS;
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
        int sampleStatus = _profilerPtr->sampleOnce(rConfig.targetPid, rConfig.sampleIntervalMs, sample);
        if (sampleStatus != 0) {
            return sampleStatus;
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
