#ifndef CLIPARSING_H
#define CLIPARSING_H

#include "CliOptions.h"
#include "ProfilingConfig.h"

/**
 * @brief Prints CLI usage and available options.
 *
 * @param pProgramName Program name from argv[0].
 */
void printUsage(const char* pProgramName);

/**
 * @brief Parses CLI options and positional arguments into runtime config.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param pOptions Parsed options output.
 * @param pConfig Parsed profiling config output.
 * @retval true Parse succeeded.
 * @retval false Parse failed.
 */
bool parseClientArguments(int argc, char** argv, CliOptions* pOptions, ProfilingConfig* pConfig);

#endif
