#include "CacheProfilerApp.h"
#include "CacheSampleLoggerConfig.h"
#include "CliOptions.h"
#include "CliParsing.h"
#include "Logger.h"

#include <cstdio>
#include <cstring>

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
    Logger::configureFromEnvironment();

    CliOptions options = {};
    ProfilingConfig config = {};
    if (!parseClientArguments(argc, argv, &options, &config)) {
        printUsage(argv[0]);
        return 1;
    }

    if (!options.terminalLogEnabled && !options.csvLogEnabled) {
        std::fprintf(stderr, "No logger selected. Use --terminal-log or --csv-log.\n");
        printUsage(argv[0]);
        return 1;
    }

    CacheSampleLoggerConfig loggerConfig = {};
    loggerConfig.terminalLogEnabled = options.terminalLogEnabled;
    loggerConfig.csvLogEnabled = options.csvLogEnabled;
    loggerConfig.csvDirectoryPath = options.csvDirectoryPath;
    loggerConfig.csvFileName = options.csvFileName;
    loggerConfig.csvFlushSampleCount = options.csvFlushSampleCount;

    CacheProfilerApp app_profiler(loggerConfig);
    int runStatus = app_profiler.run(config);
    if (runStatus != 0) {
        std::fprintf(stderr, "Cache profiling failed: %d (%s)\n", runStatus, std::strerror(-runStatus));
        return 1;
    }

    return 0;
}
