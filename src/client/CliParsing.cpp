#include "CliParsing.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
bool parseOptionsAndPositionals(int argc, char** argv, CliOptions* pOptions, std::vector<const char*>* pPositionals) {
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

}  // namespace

void printUsage(const char* pProgramName) {
    std::fprintf(stderr, "Usage: %s [options] <pid> <interval_ms> [duration_ms]\n", pProgramName);
    std::fprintf(stderr, "Options (must come before positional arguments):\n");
    std::fprintf(stderr, "  --terminal-log    Enable terminal sample logging\n");
}

bool parseClientArguments(int argc, char** argv, CliOptions* pOptions, ProfilingConfig* pConfig) {
    if ((pOptions == nullptr) || (pConfig == nullptr)) {
        return false;
    }

    std::vector<const char*> positionalArgs;
    if (!parseOptionsAndPositionals(argc, argv, pOptions, &positionalArgs)) {
        return false;
    }

    if ((positionalArgs.size() != 2) && (positionalArgs.size() != 3)) {
        return false;
    }

    pConfig->hasDurationLimit = false;
    pConfig->profileDurationMs = 0;

    // Parse and validate PID.
    if (!parsePid(positionalArgs[0], &pConfig->targetPid)) {
        std::fprintf(stderr, "Invalid <pid>: %s\n", positionalArgs[0]);
        return false;
    }

    // Sampling interval defines one profiling window length.
    if (!parseUint32(positionalArgs[1], &pConfig->sampleIntervalMs) || (pConfig->sampleIntervalMs == 0)) {
        std::fprintf(stderr, "Invalid <interval_ms>: %s\n", positionalArgs[1]);
        return false;
    }

    if (positionalArgs.size() == 3) {
        pConfig->hasDurationLimit = true;
        if (!parseUint64(positionalArgs[2], &pConfig->profileDurationMs) || (pConfig->profileDurationMs == 0)) {
            std::fprintf(stderr, "Invalid [duration_ms]: %s\n", positionalArgs[2]);
            return false;
        }
    }

    return true;
}
