#define _POSIX_C_SOURCE 200809L

#include "printmsg.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Prints one cache level line.
 *
 * @param p_name Cache level label.
 * @param p_level Cache level stats.
 */
static void print_level(const char *p_name, const struct printmsg_cache_level_stats *p_level) {
    if (!p_level->supported) {
        printf("%s: unsupported on this system\n", p_name);
        return;
    }
    printf("%s: accesses=%" PRIu64 " misses=%" PRIu64 "\n", p_name, p_level->accesses,
           p_level->misses);
}

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
    struct printmsg_cache_sampler *p_sampler = NULL;
    struct printmsg_cache_stats stats;
    unsigned int interval_ms = 1000;
    unsigned int sample_count = 5;
    pid_t pid;
    struct timespec delay;
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

    rc = printmsg_cache_sampler_create(pid, &p_sampler);
    if (rc != 0) {
        fprintf(stderr, "Failed to create sampler for PID %d: %d (%s)\n", pid, rc,
                strerror(-rc));
        return 1;
    }

    printf("Sampling PID %d every %u ms (%u samples)\n", pid, interval_ms, sample_count);

    delay.tv_sec = interval_ms / 1000U;
    delay.tv_nsec = (long)(interval_ms % 1000U) * 1000000L;

    for (unsigned int i = 0; i < sample_count; ++i) {
        rc = printmsg_cache_sampler_read(p_sampler, &stats);
        if (rc != 0) {
            fprintf(stderr, "Failed to read sample: %d (%s)\n", rc, strerror(-rc));
            printmsg_cache_sampler_destroy(p_sampler);
            return 1;
        }

        printf("Sample %u:\n", i + 1U);
        print_level("  L1", &stats.l1);
        print_level("  L2", &stats.l2);
        print_level("  LLC", &stats.llc);

        if (i + 1U < sample_count) {
            (void)nanosleep(&delay, NULL);
        }
    }

    printmsg_cache_sampler_destroy(p_sampler);
    return 0;
}
