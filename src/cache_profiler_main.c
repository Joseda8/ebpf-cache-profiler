#define _POSIX_C_SOURCE 200809L

#include "cache_profiler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    // Sensible defaults for a short interactive profile.
    unsigned int interval_ms = 1000;
    unsigned int sample_count = 5;
    pid_t pid;
    int rc;

    // Required arg: PID. Optional args: interval and sample count.
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: %s <pid> [interval_ms] [sample_count]\n", p_argv[0]);
        return 1;
    }

    // PID is positional and must be a positive integer.
    pid = (pid_t)atoi(p_argv[1]);
    if (pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", p_argv[1]);
        return 1;
    }

    // Optional overrides keep defaults when omitted.
    if (argc >= 3 && parse_u32(p_argv[2], &interval_ms) != 0) {
        fprintf(stderr, "Invalid interval_ms: %s\n", p_argv[2]);
        return 1;
    }
    if (argc >= 4 && parse_u32(p_argv[3], &sample_count) != 0) {
        fprintf(stderr, "Invalid sample_count: %s\n", p_argv[3]);
        return 1;
    }

    // Stream samples live so output appears as each sample is gathered.
    rc = cache_profiler_stream(pid, interval_ms, sample_count);
    if (rc != 0) {
        fprintf(stderr, "Failed to profile PID %d: %d (%s)\n", pid, rc, strerror(-rc));
        return 1;
    }

    return 0;
}
