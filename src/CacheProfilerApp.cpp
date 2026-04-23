#include "CacheProfilerApp.h"

#include <errno.h>
#include <signal.h>

#include <chrono>
#include <cstdio>

namespace {

volatile sig_atomic_t gStopRequested = 0;

void onStopSignal(int signalNumber) {
    // Unused signal id.
    (void)signalNumber;
    gStopRequested = 1;
}

}  // namespace

CacheProfilerApp::CacheProfilerApp(std::unique_ptr<ICacheProfiler> profilerPtrIn) : profilerPtr(std::move(profilerPtrIn)) {}

int CacheProfilerApp::run(const ProfilingConfig& rConfig) {
    if (!profilerPtr) {
        return -EINVAL;
    }

    signal(SIGINT, onStopSignal);
    signal(SIGTERM, onStopSignal);

    auto startTime = std::chrono::steady_clock::now();
    uint64_t sampleIdx = 0;

    while (gStopRequested == 0) {
        if (!isProcessAlive(rConfig.targetPid)) {
            std::fprintf(stdout, "target pid=%d exited, stopping profiler\n", static_cast<int>(rConfig.targetPid));
            return 0;
        }

        CacheSample sample = {0, 0, 0, 0, 0, 0};
        int rc = profilerPtr->sampleOnce(rConfig.targetPid, rConfig.sampleIntervalMs, sample);
        if (rc != 0) {
            return rc;
        }

        auto nowTime = std::chrono::steady_clock::now();
        uint64_t elapsedMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - startTime).count());

        std::printf(
            "sample=%llu elapsed_ms=%llu pid=%d\n"
            "  l1_read_accesses=%llu\n"
            "  l1_read_misses=%llu\n"
            "  l2_read_accesses=%llu\n"
            "  l2_read_misses=%llu\n"
            "  llc_read_accesses=%llu\n"
            "  llc_read_misses=%llu\n",
            static_cast<unsigned long long>(sampleIdx),
            static_cast<unsigned long long>(elapsedMs),
            static_cast<int>(rConfig.targetPid),
            static_cast<unsigned long long>(sample.l1ReadAccesses),
            static_cast<unsigned long long>(sample.l1ReadMisses),
            static_cast<unsigned long long>(sample.l2ReadAccesses),
            static_cast<unsigned long long>(sample.l2ReadMisses),
            static_cast<unsigned long long>(sample.llcReadAccesses),
            static_cast<unsigned long long>(sample.llcReadMisses));
        std::fflush(stdout);

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
