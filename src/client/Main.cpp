#include "CacheProfilerApp.h"
#include "CliOptions.h"
#include "CliParsing.h"

#include <errno.h>
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
    CliOptions options = {};
    ProfilingConfig config = {};
    if (!parseClientArguments(argc, argv, &options, &config)) {
        printUsage(argv[0]);
        return 1;
    }

    // Main is a client app; profiler wiring lives in the library API.
    CacheProfilerApp app(options.terminalLogEnabled);
    int runStatus = app.run(config);
    if (runStatus != 0) {
        if (runStatus == -ENOSYS) {
            std::fprintf(stderr, "No logger selected. Use --terminal-log. CSV logging is not implemented yet.\n");
            printUsage(argv[0]);
            return 1;
        }
        std::fprintf(stderr, "Cache profiling failed: %d (%s)\n", runStatus, std::strerror(-runStatus));
        return 1;
    }

    return 0;
}
