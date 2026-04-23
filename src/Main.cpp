#include "CacheProfilerApp.h"
#include "EBpfCacheProfiler.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

/**
 * @brief Parses an unsigned 32-bit value from C-string input.
 *
 * @param pRawValue Raw input string.
 * @param pOut Parsed output pointer.
 * @retval true Parse succeeded.
 * @retval false Parse failed.
 */
bool parseUint32(const char* pRawValue, uint32_t* pOut) {
    if ((pRawValue == nullptr) || (pOut == nullptr)) {
        return false;
    }

    char* pEnd = nullptr;
    unsigned long rawValue = std::strtoul(pRawValue, &pEnd, 10);
    if ((*pRawValue == '\0') || (pEnd == nullptr) || (*pEnd != '\0') || (rawValue > 0xFFFFFFFFUL)) {
        return false;
    }

    *pOut = static_cast<uint32_t>(rawValue);
    return true;
}

/**
 * @brief Parses an unsigned 64-bit value from C-string input.
 *
 * @param pRawValue Raw input string.
 * @param pOut Parsed output pointer.
 * @retval true Parse succeeded.
 * @retval false Parse failed.
 */
bool parseUint64(const char* pRawValue, uint64_t* pOut) {
    if ((pRawValue == nullptr) || (pOut == nullptr)) {
        return false;
    }

    char* pEnd = nullptr;
    unsigned long long rawValue = std::strtoull(pRawValue, &pEnd, 10);
    if ((*pRawValue == '\0') || (pEnd == nullptr) || (*pEnd != '\0')) {
        return false;
    }

    *pOut = static_cast<uint64_t>(rawValue);
    return true;
}

/**
 * @brief Parses PID from C-string input.
 *
 * @param pRawValue Raw input string.
 * @param pOut Parsed output pointer.
 * @retval true Parse succeeded.
 * @retval false Parse failed.
 */
bool parsePid(const char* pRawValue, pid_t* pOut) {
    if ((pRawValue == nullptr) || (pOut == nullptr)) {
        return false;
    }

    char* pEnd = nullptr;
    long rawValue = std::strtol(pRawValue, &pEnd, 10);
    if ((*pRawValue == '\0') || (pEnd == nullptr) || (*pEnd != '\0') || (rawValue <= 0)) {
        return false;
    }

    *pOut = static_cast<pid_t>(rawValue);
    return true;
}

}  // namespace

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
    if ((argc != 4) && (argc != 5)) {
        std::fprintf(stderr, "Usage: %s <pid> <interval_ms> <bpf_object_path> [duration_ms]\n", argv[0]);
        return 1;
    }

    ProfilingConfig config = {};
    config.hasDurationLimit = false;
    config.profileDurationMs = 0;
    config.bpfObjectPath = argv[3];

    if (!parsePid(argv[1], &config.targetPid)) {
        std::fprintf(stderr, "Invalid <pid>: %s\n", argv[1]);
        return 1;
    }

    if (!parseUint32(argv[2], &config.sampleIntervalMs) || (config.sampleIntervalMs == 0)) {
        std::fprintf(stderr, "Invalid <interval_ms>: %s\n", argv[2]);
        return 1;
    }

    if (argc == 5) {
        config.hasDurationLimit = true;
        if (!parseUint64(argv[4], &config.profileDurationMs) || (config.profileDurationMs == 0)) {
            std::fprintf(stderr, "Invalid [duration_ms]: %s\n", argv[4]);
            return 1;
        }
    }

    auto profilerPtr = std::make_unique<EBpfCacheProfiler>(config.bpfObjectPath);
    CacheProfilerApp app(std::move(profilerPtr));
    int rc = app.run(config);
    if (rc != 0) {
        std::fprintf(stderr, "Cache profiling failed: %d (%s)\n", rc, std::strerror(-rc));
        return 1;
    }

    return 0;
}
