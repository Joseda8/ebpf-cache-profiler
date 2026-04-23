#include "EBpfCacheProfiler.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

/**
 * @brief Program entry point for PID cache profiling via eBPF.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * @return Process exit status.
 * @retval 0 Success.
 * @retval 1 Failure.
 */
int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "Usage: %s <pid> <interval_ms> <bpf_object_path>\n", argv[0]);
        return 1;
    }

    pid_t targetPid = static_cast<pid_t>(std::strtol(argv[1], nullptr, 10));
    uint32_t intervalMs = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    std::string bpfObjectPath = argv[3];

    EBpfCacheProfiler profiler(bpfObjectPath);
    CacheSample sample = {0, 0, 0, 0, 0, 0};
    int rc = profiler.sampleOnce(targetPid, intervalMs, sample);
    if (rc != 0) {
        std::fprintf(stderr, "Cache profiling failed: %d (%s)\n", rc, std::strerror(-rc));
        return 1;
    }

    std::printf("pid=%d interval_ms=%u\n", static_cast<int>(targetPid), intervalMs);
    std::printf("  l1_read_accesses=%llu\n", static_cast<unsigned long long>(sample.l1ReadAccesses));
    std::printf("  l1_read_misses=%llu\n", static_cast<unsigned long long>(sample.l1ReadMisses));
    std::printf("  l2_read_accesses=%llu\n", static_cast<unsigned long long>(sample.l2ReadAccesses));
    std::printf("  l2_read_misses=%llu\n", static_cast<unsigned long long>(sample.l2ReadMisses));
    std::printf("  llc_read_accesses=%llu\n", static_cast<unsigned long long>(sample.llcReadAccesses));
    std::printf("  llc_read_misses=%llu\n", static_cast<unsigned long long>(sample.llcReadMisses));

    return 0;
}
