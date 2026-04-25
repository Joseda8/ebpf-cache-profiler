#include "CacheProfilerApp.h"
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

    if (!options.terminalLogEnabled) {
        std::fprintf(stderr, "No logger selected. Use --terminal-log. CSV logging is not implemented yet.\n");
        printUsage(argv[0]);
        return 1;
    }

    CacheProfilerApp app_profiler(options.terminalLogEnabled);
    int runStatus = app_profiler.run(config);
    if (runStatus != 0) {
        std::fprintf(stderr, "Cache profiling failed: %d (%s)\n", runStatus, std::strerror(-runStatus));
        return 1;
    }

    return 0;
}
