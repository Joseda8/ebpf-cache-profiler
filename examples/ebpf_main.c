#define _POSIX_C_SOURCE 200809L

#include "printmsg.h"

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
 * @brief Example cache sampler for a target PID.
 *
 * @param argc Number of command-line arguments.
 * @param p_argv Command-line arguments.
 *
 * @return Process exit status.
 * @retval 0 Success.
 * @retval 1 Failure.
 */
int main(int argc, char **p_argv) {
    struct printmsg_cache_stats *p_stats_array = NULL;
    unsigned int interval_ms = 1000;
    unsigned int sample_count = 5;
    pid_t pid;
    int rc;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: %s <pid> [interval_ms] [sample_count]\n", p_argv[0]);
        return 1;
    }

    pid = (pid_t)atoi(p_argv[1]);
    if (pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", p_argv[1]);
        return 1;
    }

    if (argc >= 3 && parse_u32(p_argv[2], &interval_ms) != 0) {
        fprintf(stderr, "Invalid interval_ms: %s\n", p_argv[2]);
        return 1;
    }
    if (argc >= 4 && parse_u32(p_argv[3], &sample_count) != 0) {
        fprintf(stderr, "Invalid sample_count: %s\n", p_argv[3]);
        return 1;
    }

    // The library fills one struct per requested sample.
    p_stats_array = calloc((size_t)sample_count, sizeof(struct printmsg_cache_stats));
    if (p_stats_array == NULL) {
        fprintf(stderr, "Failed to allocate sample buffer\n");
        return 1;
    }

    rc = printmsg_cache_profile_capture(pid, interval_ms, sample_count, p_stats_array);
    if (rc != 0) {
        fprintf(stderr, "Failed to profile PID %d: %d (%s)\n", pid, rc, strerror(-rc));
        free(p_stats_array);
        return 1;
    }

    // Keep reporting centralized in the library so the CLI remains thin.
    printmsg_cache_profile_report(pid, interval_ms, sample_count, p_stats_array);
    free(p_stats_array);
    return 0;
}
