#ifndef CSVCACHESAMPLELOGGER_H
#define CSVCACHESAMPLELOGGER_H

#include "ICacheSampleLogger.h"

#include <stdint.h>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Buffered CSV logger for cumulative cache samples.
 */
class CsvCacheSampleLogger : public ICacheSampleLogger {
public:
    /**
     * @brief Creates a CSV logger and opens its output file.
     *
     * @param rDirectoryPath Destination directory.
     * @param rFileName Output file name.
     * @param flushSampleCount Number of samples to buffer before flush.
     * @param rLoggerOutput Logger output ownership.
     *
     * @retval 0 Success.
     * @retval Negative errno code Failure.
     */
    static int create(
        const std::string& rDirectoryPath,
        const std::string& rFileName,
        uint32_t flushSampleCount,
        std::unique_ptr<CsvCacheSampleLogger>& rLoggerOutput);

    ~CsvCacheSampleLogger() override;

    void logSample(uint64_t sampleIdx, uint64_t elapsedMs, pid_t targetPid, const CacheSample& rSample) override;
    void logTargetExit(pid_t targetPid) override;

private:
    CsvCacheSampleLogger(const std::string& rOutputPath, uint32_t flushSampleCount);
    int writeHeader();
    int flushPendingRows();

    std::string _outputPath;
    std::ofstream _outputFileStream;
    std::vector<std::string> _pendingRows;
    uint32_t _flushSampleCount;
};

#endif
