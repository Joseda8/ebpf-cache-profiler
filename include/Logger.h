#ifndef LOGGER_H
#define LOGGER_H

#include <string_view>

/**
 * @brief Logging utility with severity filtering.
 */
class Logger {
public:
    enum class Level {
        kDebug = 0,
        kInfo = 1,
        kWarning = 2,
        kError = 3,
    };

    /**
     * @brief Configures logger level from CACHE_PROFILER_LOG_LEVEL.
     *
     * Supported values: debug, info, warning, error.
     */
    static void configureFromEnvironment();

    /**
     * @brief Sets active log level threshold.
     *
     * @param level Minimum level that will be emitted.
     */
    static void setLevel(Level level);

    /**
     * @brief Emits a debug-level message.
     *
     * @param rMessage Message text.
     */
    static void debug(std::string_view rMessage);

    /**
     * @brief Emits an info-level message.
     *
     * @param rMessage Message text.
     */
    static void info(std::string_view rMessage);

    /**
     * @brief Emits a warning-level message.
     *
     * @param rMessage Message text.
     */
    static void warning(std::string_view rMessage);

    /**
     * @brief Emits an error-level message.
     *
     * @param rMessage Message text.
     */
    static void error(std::string_view rMessage);

private:
    static void log(Level level, std::string_view rMessage);
    static bool shouldLog(Level level);
    static const char* levelToString(Level level);
    static Level parseLevel(const char* pRawLevel);
};

#endif
