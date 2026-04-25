#include "CsvCacheSampleLogger.h"

#include <errno.h>
#include <sys/types.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

CsvCacheSampleLogger::CsvCacheSampleLogger(const std::string& rOutputPath, uint32_t flushSampleCount)
    : _outputPath(rOutputPath), _outputFileStream(), _pendingRows(), _flushSampleCount(flushSampleCount) {
}

CsvCacheSampleLogger::~CsvCacheSampleLogger() {
    flushPendingRows();
}

int CsvCacheSampleLogger::create(
    const std::string& rDirectoryPath,
    const std::string& rFileName,
    uint32_t flushSampleCount,
    std::unique_ptr<CsvCacheSampleLogger>& rLoggerOutput) {
    if (rFileName.empty() || (flushSampleCount == 0)) {
        return -EINVAL;
    }

    std::filesystem::path directoryPath = rDirectoryPath.empty() ? std::filesystem::path(".") : std::filesystem::path(rDirectoryPath);
    std::error_code createDirectoryErr;
    std::filesystem::create_directories(directoryPath, createDirectoryErr);
    if (createDirectoryErr) {
        return -createDirectoryErr.value();
    }

    std::filesystem::path outputPath = directoryPath / rFileName;
    std::unique_ptr<CsvCacheSampleLogger> loggerPtr = std::unique_ptr<CsvCacheSampleLogger>(
        new CsvCacheSampleLogger(outputPath.string(), flushSampleCount));

    loggerPtr->_outputFileStream.open(outputPath, std::ios::out | std::ios::trunc);
    if (!loggerPtr->_outputFileStream.is_open()) {
        return -errno;
    }

    int headerStatus = loggerPtr->writeHeader();
    if (headerStatus != 0) {
        return headerStatus;
    }

    rLoggerOutput = std::move(loggerPtr);
    return 0;
}

void CsvCacheSampleLogger::logSample(uint64_t sampleIdx, uint64_t elapsedMs, pid_t targetPid, const CacheSample& rSample) {
    char rowBuffer[512] = {};
    int rowSize = std::snprintf(
        rowBuffer,
        sizeof(rowBuffer),
        "%llu,%llu,%d,%llu,%llu,%llu,%llu,%llu,%llu\n",
        static_cast<unsigned long long>(sampleIdx),
        static_cast<unsigned long long>(elapsedMs),
        static_cast<int>(targetPid),
        static_cast<unsigned long long>(rSample.l1ReadAccessTotal),
        static_cast<unsigned long long>(rSample.l1ReadMissTotal),
        static_cast<unsigned long long>(rSample.l2ReadAccessTotal),
        static_cast<unsigned long long>(rSample.l2ReadMissTotal),
        static_cast<unsigned long long>(rSample.llcReadAccessTotal),
        static_cast<unsigned long long>(rSample.llcReadMissTotal));
    if (rowSize <= 0) {
        return;
    }

    _pendingRows.emplace_back(rowBuffer);
    if (_pendingRows.size() >= static_cast<size_t>(_flushSampleCount)) {
        flushPendingRows();
    }
}

void CsvCacheSampleLogger::logTargetExit(pid_t targetPid) {
    (void)targetPid;
    flushPendingRows();
}

int CsvCacheSampleLogger::writeHeader() {
    if (!_outputFileStream.is_open()) {
        return -EINVAL;
    }

    _outputFileStream
        << "sample_idx,elapsed_ms,pid,l1_read_access_total,l1_read_miss_total,"
        << "l2_read_access_total,l2_read_miss_total,llc_read_access_total,llc_read_miss_total\n";
    if (_outputFileStream.fail()) {
        return -EIO;
    }
    _outputFileStream.flush();
    if (_outputFileStream.fail()) {
        return -EIO;
    }

    return 0;
}

int CsvCacheSampleLogger::flushPendingRows() {
    if (!_outputFileStream.is_open()) {
        return -EINVAL;
    }

    for (const std::string& rRow : _pendingRows) {
        _outputFileStream << rRow;
    }
    _outputFileStream.flush();
    if (_outputFileStream.fail()) {
        return -EIO;
    }

    _pendingRows.clear();
    return 0;
}
