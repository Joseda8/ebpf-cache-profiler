#include "Logger.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <mutex>
#include <string>

namespace {

Logger::Level gLogLevel = Logger::Level::kInfo;
std::mutex gLogMutex;

std::string toLowerCopy(const char* pRawValue) {
    if (pRawValue == nullptr) {
        return "";
    }

    std::string loweredValue(pRawValue);
    for (size_t charIdx = 0; charIdx < loweredValue.size(); ++charIdx) {
        loweredValue[charIdx] = static_cast<char>(tolower(static_cast<unsigned char>(loweredValue[charIdx])));
    }

    return loweredValue;
}

}  // namespace

void Logger::configureFromEnvironment() {
    const char* pRawLevel = getenv("CACHE_PROFILER_LOG_LEVEL");
    if (pRawLevel == nullptr) {
        return;
    }

    setLevel(parseLevel(pRawLevel));
}

void Logger::setLevel(Level level) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    gLogLevel = level;
}

void Logger::debug(std::string_view rMessage) {
    log(Level::kDebug, rMessage);
}

void Logger::info(std::string_view rMessage) {
    log(Level::kInfo, rMessage);
}

void Logger::warning(std::string_view rMessage) {
    log(Level::kWarning, rMessage);
}

void Logger::error(std::string_view rMessage) {
    log(Level::kError, rMessage);
}

void Logger::log(Level level, std::string_view rMessage) {
    if (!shouldLog(level)) {
        return;
    }

    std::lock_guard<std::mutex> lock(gLogMutex);

    time_t nowEpoch = time(nullptr);
    struct tm nowTm = {};
    localtime_r(&nowEpoch, &nowTm);
    char timeBuffer[20] = {};
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &nowTm);

    fprintf(
        stderr,
        "[%s] [%s] %.*s\n",
        timeBuffer,
        levelToString(level),
        static_cast<int>(rMessage.size()),
        rMessage.data());
}

bool Logger::shouldLog(Level level) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    return static_cast<int>(level) >= static_cast<int>(gLogLevel);
}

const char* Logger::levelToString(Level level) {
    if (level == Level::kDebug) {
        return "DEBUG";
    }
    if (level == Level::kInfo) {
        return "INFO";
    }
    if (level == Level::kWarning) {
        return "WARNING";
    }
    return "ERROR";
}

Logger::Level Logger::parseLevel(const char* pRawLevel) {
    std::string loweredLevel = toLowerCopy(pRawLevel);

    if (loweredLevel == "debug") {
        return Level::kDebug;
    }
    if (loweredLevel == "warning") {
        return Level::kWarning;
    }
    if (loweredLevel == "error") {
        return Level::kError;
    }
    return Level::kInfo;
}
