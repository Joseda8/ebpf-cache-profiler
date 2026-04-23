#include "TerminalCacheSampleLogger.h"

#include <cstdio>

void TerminalCacheSampleLogger::logSample(uint64_t sampleIdx, uint64_t elapsedMs, pid_t targetPid, const CacheSample& rSample) {
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
        static_cast<int>(targetPid),
        static_cast<unsigned long long>(rSample.l1ReadAccesses),
        static_cast<unsigned long long>(rSample.l1ReadMisses),
        static_cast<unsigned long long>(rSample.l2ReadAccesses),
        static_cast<unsigned long long>(rSample.l2ReadMisses),
        static_cast<unsigned long long>(rSample.llcReadAccesses),
        static_cast<unsigned long long>(rSample.llcReadMisses));
    std::fflush(stdout);
}

void TerminalCacheSampleLogger::logTargetExit(pid_t targetPid) {
    std::fprintf(stdout, "target pid=%d exited, stopping profiler\n", static_cast<int>(targetPid));
}
