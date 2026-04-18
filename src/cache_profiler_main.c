#define _POSIX_C_SOURCE 200809L

#include "cache_profiler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct cache_profiler_cli_config {
    pid_t pid;
    unsigned int interval_ms;
    unsigned int sample_count;
    int terminal_log_enabled;
};

/**
 * @brief Parses a positive integer argument.
 *
 * @param p_arg Input string.
 * @param p_value Output parsed value.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval 1 Parse error.
 */
static int parse_u32(const char *p_arg, unsigned int *p_value) {
    char *p_end = NULL;
    unsigned long parsed;

    if (p_arg == NULL || p_value == NULL) {
        return 1;
    }

    errno = 0;
    parsed = strtoul(p_arg, &p_end, 10);
    if (errno != 0 || p_end == p_arg || *p_end != '\0' || parsed == 0 || parsed > 0xFFFFFFFFUL) {
        return 1;
    }

    *p_value = (unsigned int)parsed;
    return 0;
}

/**
 * @brief Parses one CLI option token.
 *
 * @param p_arg Option token (must start with "--").
 * @param p_config Mutable CLI configuration.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval 1 Parse error.
 */
static int parse_option(const char *p_arg, struct cache_profiler_cli_config *p_config) {
    if (p_arg == NULL || p_config == NULL) {
        return 1;
    }

    // Terminal logging is opt-in and represented as a boolean flag.
    if (strcmp(p_arg, "--terminal-log") == 0) {
        p_config->terminal_log_enabled = 1;
        return 0;
    }

    return 1;
}

/**
 * @brief Parses one positional CLI token.
 *
 * Position order:
 * 0 -> pid
 * 1 -> interval_ms
 * 2 -> sample_count
 *
 * @param p_arg Positional token.
 * @param positional_index Zero-based positional index.
 * @param p_config Mutable CLI configuration.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval 1 Parse error.
 */
static int parse_positional(const char *p_arg, size_t positional_index, struct cache_profiler_cli_config *p_config) {
    unsigned int value = 0;

    if (p_arg == NULL || p_config == NULL) {
        return 1;
    }
    if (parse_u32(p_arg, &value) != 0) {
        return 1;
    }

    if (positional_index == 0U) {
        p_config->pid = (pid_t)value;
        return 0;
    }
    if (positional_index == 1U) {
        p_config->interval_ms = value;
        return 0;
    }
    if (positional_index == 2U) {
        p_config->sample_count = value;
        return 0;
    }

    return 1;
}

/**
 * @brief Program entry point for cache_profiler.
 *
 * @param argc Number of command-line arguments.
 * @param p_argv Command-line arguments.
 *
 * @return Process exit status.
 * @retval 0 Success.
 * @retval 1 Failure.
 */
int main(int argc, char **p_argv) {
    struct cache_profiler_cli_config config;
    size_t positional_count = 0;
    int parsing_options = 1;
    int rc;

    // Default values keep the interface compact for common usage.
    config.pid = 0;
    config.interval_ms = 1000;
    config.sample_count = 5;
    config.terminal_log_enabled = 0;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--terminal-log] <pid> [interval_ms] [sample_count]\n", p_argv[0]);
        return 1;
    }

    // Parse options first, then positional arguments.
    for (int idx = 1; idx < argc; ++idx) {
        if (strncmp(p_argv[idx], "--", 2) == 0) {
            if (!parsing_options) {
                fprintf(stderr, "Invalid option order: %s\n", p_argv[idx]);
                fprintf(stderr, "All options must appear before positional arguments.\n");
                return 1;
            }
            if (parse_option(p_argv[idx], &config) != 0) {
                fprintf(stderr, "Invalid option: %s\n", p_argv[idx]);
                fprintf(stderr, "Supported options: --terminal-log\n");
                return 1;
            }
            continue;
        }

        parsing_options = 0;

        if (parse_positional(p_argv[idx], positional_count, &config) != 0) {
            if (positional_count == 0U) {
                fprintf(stderr, "Invalid PID: %s\n", p_argv[idx]);
                return 1;
            }
            if (positional_count == 1U) {
                fprintf(stderr, "Invalid interval_ms: %s\n", p_argv[idx]);
                return 1;
            }
            if (positional_count == 2U) {
                fprintf(stderr, "Invalid sample_count: %s\n", p_argv[idx]);
                return 1;
            }
            fprintf(stderr, "Too many positional arguments: %s\n", p_argv[idx]);
            return 1;
        }

        positional_count += 1U;
    }

    // PID is required even though interval/sample have defaults.
    if (config.pid <= 0) {
        fprintf(stderr, "Missing required PID.\n");
        fprintf(stderr, "Usage: %s [--terminal-log] <pid> [interval_ms] [sample_count]\n", p_argv[0]);
        return 1;
    }

    // CSV sink is not available yet, so disabled terminal logging cannot continue.
    if (!config.terminal_log_enabled) {
        fprintf(stderr, "Terminal logging is disabled and CSV output is not implemented yet.\n");
        fprintf(stderr, "Re-run with --terminal-log to display samples in the terminal.\n");
        return 1;
    }

    // Stream samples live because terminal logging is explicitly enabled.
    rc = cache_profiler_stream(config.pid, config.interval_ms, config.sample_count);
    if (rc != 0) {
        fprintf(stderr, "Failed to profile PID %d: %d (%s)\n", config.pid, rc, strerror(-rc));
        return 1;
    }

    return 0;
}
