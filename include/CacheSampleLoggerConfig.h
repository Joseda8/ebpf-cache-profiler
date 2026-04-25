#ifndef CACHESAMPLELOGGERCONFIG_H
#define CACHESAMPLELOGGERCONFIG_H

#include <stdint.h>

#include <string>

/**
 * @brief Logger backend selection and CSV runtime options.
 */
struct CacheSampleLoggerConfig {
    // Enables terminal sample logging when true.
    bool terminalLogEnabled;

    // Enables CSV sample logging when true.
    bool csvLogEnabled;

    // Destination directory for CSV output.
    std::string csvDirectoryPath;

    // CSV file name.
    std::string csvFileName;

    // Number of samples buffered before writing to disk.
    uint32_t csvFlushSampleCount;
};

#endif
