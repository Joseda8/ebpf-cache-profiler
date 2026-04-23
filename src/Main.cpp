#include "CacheProfilerApp.h"
#include "EBpfCacheProfiler.h"
#include "TerminalCacheSampleLogger.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

/**
 * @brief Command line options parsed before positional arguments.
 */
struct CliOptions {
    bool terminalLogEnabled;
};

/**
 * @brief Prints CLI usage and available options.
 *
 * @param pProgramName Program name from argv[0].
 */
void printUsage(const char* pProgramName) {
    std::fprintf(stderr, "Usage: %s [options] <pid> <interval_ms> <bpf_object_path> [duration_ms]\n", pProgramName);
    std::fprintf(stderr, "Options (must come before positional arguments):\n");
    std::fprintf(stderr, "  --terminal-log    Enable terminal sample logging\n");
}

/**
 * @brief Parses options and positional arguments using options-first contract.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param pOptions Parsed options output.
 * @param pPositionals Parsed positional output.
 * @retval true Parse succeeded.
 * @retval false Parse failed.
 */
bool parseCommandLine(int argc, char** argv, CliOptions* pOptions, std::vector<const char*>* pPositionals) {
    if ((pOptions == nullptr) || (pPositionals == nullptr)) {
        return false;
    }

    pOptions->terminalLogEnabled = false;
    pPositionals->clear();

    bool parsingOptions = true;
    for (int argIdx = 1; argIdx < argc; ++argIdx) {
        const char* pArg = argv[argIdx];
        if (pArg == nullptr) {
            return false;
        }

        const bool isOption = (std::strncmp(pArg, "--", 2) == 0);
        if (parsingOptions && isOption) {
            if (std::strcmp(pArg, "--terminal-log") == 0) {
                pOptions->terminalLogEnabled = true;
                continue;
            }

            std::fprintf(stderr, "Unknown option: %s\n", pArg);
            return false;
        }

        parsingOptions = false;
        if (isOption) {
            std::fprintf(stderr, "Options must appear before positional arguments: %s\n", pArg);
            return false;
        }

        pPositionals->push_back(pArg);
    }

    return true;
}

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
    CliOptions options = {};
    std::vector<const char*> positionalArgs;
    if (!parseCommandLine(argc, argv, &options, &positionalArgs)) {
        printUsage(argv[0]);
        return 1;
    }

    if ((positionalArgs.size() != 3) && (positionalArgs.size() != 4)) {
        printUsage(argv[0]);
        return 1;
    }

    // Terminal logging is currently the only implemented sink.
    if (!options.terminalLogEnabled) {
        std::fprintf(
            stderr,
            "No logger selected. Use --terminal-log. CSV logging is not implemented yet.\n");
        printUsage(argv[0]);
        return 1;
    }

    ProfilingConfig config = {};
    config.hasDurationLimit = false;
    config.profileDurationMs = 0;
    config.bpfObjectPath = positionalArgs[2];

    // Parse and validate PID
    if (!parsePid(positionalArgs[0], &config.targetPid)) {
        std::fprintf(stderr, "Invalid <pid>: %s\n", positionalArgs[0]);
        return 1;
    }

    // Sampling interval defines one profiling window length.
    if (!parseUint32(positionalArgs[1], &config.sampleIntervalMs) || (config.sampleIntervalMs == 0)) {
        std::fprintf(stderr, "Invalid <interval_ms>: %s\n", positionalArgs[1]);
        return 1;
    }

    if (positionalArgs.size() == 4) {
        config.hasDurationLimit = true;
        if (!parseUint64(positionalArgs[3], &config.profileDurationMs) || (config.profileDurationMs == 0)) {
            std::fprintf(stderr, "Invalid [duration_ms]: %s\n", positionalArgs[3]);
            return 1;
        }
    }

    // Main only wires dependencies; runtime loop lives in CacheProfilerApp.
    auto profilerPtr = std::make_unique<EBpfCacheProfiler>(config.bpfObjectPath);
    auto loggerPtr = std::make_unique<TerminalCacheSampleLogger>();
    CacheProfilerApp app(std::move(profilerPtr), std::move(loggerPtr));
    int rc = app.run(config);
    if (rc != 0) {
        std::fprintf(stderr, "Cache profiling failed: %d (%s)\n", rc, std::strerror(-rc));
        return 1;
    }

    return 0;
}
