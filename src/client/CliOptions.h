#ifndef CLIOPTIONS_H
#define CLIOPTIONS_H

#include <stdint.h>

#include <string>

/**
 * @brief Command line options parsed before positional arguments.
 */
struct CliOptions {
    bool terminalLogEnabled;
    bool csvLogEnabled;
    std::string csvDirectoryPath;
    std::string csvFileName;
    uint32_t csvFlushSampleCount;
};

#endif
