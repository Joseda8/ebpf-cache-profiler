#include "CacheProfilerApp.h"
#include "CsvCacheSampleLogger.h"
#include "EBpfCacheProfiler.h"
#include "Logger.h"
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

CacheProfilerApp::CacheProfilerApp(const CacheSampleLoggerConfig& rLoggerConfig)
    : _loggerConfig(rLoggerConfig),
      _profilerPtr(std::make_unique<EBpfCacheProfiler>(resolveDefaultBpfObjectPath())),
      _csvLoggerPtr(nullptr),
      _loggerSetupStatus(0) {
    if (!_loggerConfig.terminalLogEnabled && !_loggerConfig.csvLogEnabled) {
        _loggerSetupStatus = -ENOSYS;
        return;
    }

    if (_loggerConfig.csvLogEnabled) {
        _loggerSetupStatus = CsvCacheSampleLogger::create(
            _loggerConfig.csvDirectoryPath, _loggerConfig.csvFileName, _loggerConfig.csvFlushSampleCount, _csvLoggerPtr
        );
    }
}

int CacheProfilerApp::run(const ProfilingConfig& rConfig) {
    // Validate required runtime dependencies.
    if (!_profilerPtr) {
        Logger::error("CacheProfilerApp run failed: profiler dependency is not configured");
        return -EINVAL;
    }
    if (_loggerSetupStatus != 0) {
        Logger::error("CacheProfilerApp run failed: logger setup status=" + std::to_string(_loggerSetupStatus));
        return _loggerSetupStatus;
    }
    TerminalCacheSampleLogger terminalLogger;

    // Register termination handlers once per run invocation so Ctrl+C and
    // SIGTERM can stop the sampling loop cleanly.
    signal(SIGINT, onStopSignal);
    signal(SIGTERM, onStopSignal);

    Logger::info(
        "Starting profiling run: pid=" + std::to_string(static_cast<int>(rConfig.targetPid)) +
        " interval_ms=" + std::to_string(rConfig.sampleIntervalMs) +
        " duration_ms=" + (rConfig.hasDurationLimit ? std::to_string(rConfig.profileDurationMs) : std::string("unbounded")));

    // Initialize profiler resources and bind target PID
    int initializationStatus = _profilerPtr->initializeProfiling(rConfig.targetPid);
    if (initializationStatus != 0) {
        Logger::error("Profiler initialization failed: status=" + std::to_string(initializationStatus));
        return initializationStatus;
    }
    Logger::debug("Profiler initialization completed successfully");
    Logger::info("Cumulative sampling mode active: each sample reports totals since profiling initialization");

    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    uint64_t sampleIdx = 0;

    // Run one cumulative snapshot per interval. Values are monotonic while
    // the target remains active and counters remain readable.
    while (gStopRequested == 0) {
        // Verify that the process is alive before profiling
        if (!isProcessAlive(rConfig.targetPid)) {
            Logger::info("Target process exited. Stopping profiling loop");
            if (_loggerConfig.terminalLogEnabled) {
                terminalLogger.logTargetExit(rConfig.targetPid);
            }
            if (_loggerConfig.csvLogEnabled && _csvLoggerPtr) {
                _csvLoggerPtr->logTargetExit(rConfig.targetPid);
            }
            return 0;
        }

        CacheSample sample = {0, 0, 0, 0, 0, 0};
        int sampleStatus = _profilerPtr->sampleOnce(rConfig.sampleIntervalMs, sample);
        if (sampleStatus != 0) {
            Logger::error("Sampling failed at sample_idx=" + std::to_string(sampleIdx) + " status=" + std::to_string(sampleStatus));
            return sampleStatus;
        }

        std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
        uint64_t elapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - startTime).count());

        if (_loggerConfig.terminalLogEnabled) {
            terminalLogger.logSample(sampleIdx, elapsedMs, rConfig.targetPid, sample);
        }
        if (_loggerConfig.csvLogEnabled && _csvLoggerPtr) {
            _csvLoggerPtr->logSample(sampleIdx, elapsedMs, rConfig.targetPid, sample);
        }

        ++sampleIdx;

        if (rConfig.hasDurationLimit && (elapsedMs >= rConfig.profileDurationMs)) {
            Logger::info("Duration limit reached; stopping profiling loop");
            return 0;
        }
    }

    // Loop ended because a stop signal was received.
    Logger::info("Stop signal received; profiling loop terminated");
    return 0;
}

bool CacheProfilerApp::isProcessAlive(pid_t targetPid) const {
    if (kill(targetPid, 0) == 0) {
        return true;
    }

    return (errno != ESRCH);
}
