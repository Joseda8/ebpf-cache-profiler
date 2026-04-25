#include "CliParsing.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
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
    pOptions->csvLogEnabled = false;
    pOptions->csvDirectoryPath = ".";
    pOptions->csvFileName.clear();
    pOptions->csvFlushSampleCount = 10;
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
            if (std::strcmp(pArg, "--csv-log") == 0) {
                pOptions->csvLogEnabled = true;
                continue;
            }
            if (std::strcmp(pArg, "--csv-path") == 0) {
                if ((argIdx + 1) >= argc) {
                    std::fprintf(stderr, "Missing value for --csv-path\n");
                    return false;
                }

                pOptions->csvDirectoryPath = argv[++argIdx];
                pOptions->csvLogEnabled = true;
                continue;
            }
            if (std::strcmp(pArg, "--csv-filename") == 0) {
                if ((argIdx + 1) >= argc) {
                    std::fprintf(stderr, "Missing value for --csv-filename\n");
                    return false;
                }

                pOptions->csvFileName = argv[++argIdx];
                pOptions->csvLogEnabled = true;
                continue;
            }
            if (std::strcmp(pArg, "--csv-flush-samples") == 0) {
                if ((argIdx + 1) >= argc) {
                    std::fprintf(stderr, "Missing value for --csv-flush-samples\n");
                    return false;
                }

                uint32_t flushSampleCount = 0;
                if (!parseUint32(argv[++argIdx], &flushSampleCount) || (flushSampleCount == 0)) {
                    std::fprintf(stderr, "Invalid --csv-flush-samples value\n");
                    return false;
                }

                pOptions->csvFlushSampleCount = flushSampleCount;
                pOptions->csvLogEnabled = true;
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

std::string buildDefaultCsvFileName(pid_t targetPid) {
    time_t nowEpoch = time(nullptr);
    struct tm nowTm = {};
    localtime_r(&nowEpoch, &nowTm);

    char timestampBuffer[32] = {};
    strftime(timestampBuffer, sizeof(timestampBuffer), "%Y%m%dT%H%M%S", &nowTm);
    return std::string(timestampBuffer) + "_" + std::to_string(static_cast<int>(targetPid));
}

}  // namespace

void printUsage(const char* pProgramName) {
    std::fprintf(stderr, "Usage: %s [options] <pid> <interval_ms> [duration_ms]\n", pProgramName);
    std::fprintf(stderr, "Options (must come before positional arguments):\n");
    std::fprintf(stderr, "  --terminal-log                       Enable terminal sample logging\n");
    std::fprintf(stderr, "  --csv-log                            Enable CSV sample logging\n");
    std::fprintf(stderr, "  --csv-path <dir>                     CSV output directory (default: .)\n");
    std::fprintf(stderr, "  --csv-filename <name>                CSV output file name (default: YYYYMMDDTHHMMSS_PID)\n");
    std::fprintf(stderr, "  --csv-flush-samples <count>          Buffered write batch size (default: 10)\n");
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

    if (!pOptions->csvFileName.empty()) {
        if ((pOptions->csvFileName.find('/') != std::string::npos) ||
            (pOptions->csvFileName.find('\\') != std::string::npos)) {
            std::fprintf(stderr, "--csv-filename must not contain path separators\n");
            return false;
        }
    }

    if (pOptions->csvLogEnabled && pOptions->csvFileName.empty()) {
        pOptions->csvFileName = buildDefaultCsvFileName(pConfig->targetPid);
    }

    return true;
}
