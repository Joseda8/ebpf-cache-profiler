#include "TerminalCacheSampleLogger.h"

#include <cstdio>

void TerminalCacheSampleLogger::logSample(uint64_t sampleIdx, uint64_t elapsedMs, pid_t targetPid, const CacheSample& rSample) {
    std::printf(
        "sample=%llu elapsed_ms=%llu pid=%d\n"
        "  l1_read_access_total=%llu\n"
        "  l1_read_miss_total=%llu\n"
        "  l2_read_access_total=%llu\n"
        "  l2_read_miss_total=%llu\n"
        "  llc_read_access_total=%llu\n"
        "  llc_read_miss_total=%llu\n",
        static_cast<unsigned long long>(sampleIdx),
        static_cast<unsigned long long>(elapsedMs),
        static_cast<int>(targetPid),
        static_cast<unsigned long long>(rSample.l1ReadAccessTotal),
        static_cast<unsigned long long>(rSample.l1ReadMissTotal),
        static_cast<unsigned long long>(rSample.l2ReadAccessTotal),
        static_cast<unsigned long long>(rSample.l2ReadMissTotal),
        static_cast<unsigned long long>(rSample.llcReadAccessTotal),
        static_cast<unsigned long long>(rSample.llcReadMissTotal));
    std::fflush(stdout);
}

void TerminalCacheSampleLogger::logTargetExit(pid_t targetPid) {
    std::fprintf(stdout, "target pid=%d exited, stopping profiler\n", static_cast<int>(targetPid));
}
