#include "CacheProfiler.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr pid_t kTargetPid = 816398;
constexpr uint32_t kIntervalMs = 500;
constexpr const char* kBpfObjectPath = "./build/cache_sampler.bpf.o";

}  // namespace

/**
 * @brief Program entry point for PID cache profiling via eBPF.
 *
 * @return Process exit status.
 * @retval 0 Success.
 * @retval 1 Failure.
 */
int main() {
    std::string bpfObjectPath = kBpfObjectPath;
    CacheSample sample = {0, 0};
    int rc = CacheProfiler::sampleOnce(kTargetPid, kIntervalMs, bpfObjectPath, sample);
    if (rc != 0) {
        std::fprintf(stderr, "Cache profiling failed: %d (%s)\n", rc, std::strerror(-rc));
        return 1;
    }

    std::printf("pid=%d interval_ms=%u\n", static_cast<int>(kTargetPid), kIntervalMs);
    std::printf("  l1_read_accesses=%llu\n", static_cast<unsigned long long>(sample.l1ReadAccesses));
    std::printf("  l1_read_misses=%llu\n", static_cast<unsigned long long>(sample.l1ReadMisses));

    return 0;
}
