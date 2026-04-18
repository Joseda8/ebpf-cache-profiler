#define _GNU_SOURCE

#include "logger.h"
#include "profiler.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

/**
 * @brief Prints one cache level from one sample.
 *
 * @param p_name Cache level label.
 * @param p_level Cache level counters.
 */
static void printmsg_print_level(const char *p_name, const struct printmsg_cache_level_stats *p_level) {
    if (!p_level->supported) {
        printf("%s: unsupported on this system\n", p_name);
        return;
    }

    printf("%s: accesses=%" PRIu64 " misses=%" PRIu64 "\n", p_name, p_level->accesses, p_level->misses);
}

/**
 * @brief Prints one sample row for stream output callback.
 *
 * @param sample_index Zero-based sample index.
 * @param sample_count Total requested sample count.
 * @param p_stats Sample stats payload.
 * @param p_user_data Unused callback context.
 *
 * @return Status code.
 * @retval 0 Success.
 */
static int printmsg_stream_sample_callback(uint32_t sample_index, uint32_t sample_count, const struct printmsg_cache_stats *p_stats, void *p_user_data) {
    (void)sample_count;
    (void)p_user_data;

    printf("Sample %" PRIu32 ":\n", sample_index + 1U);
    printmsg_print_level("  L1", &p_stats->l1);
    printmsg_print_level("  L2", &p_stats->l2);
    printmsg_print_level("  LLC", &p_stats->llc);
    // Flush so each sample appears immediately in interactive terminals.
    (void)fflush(stdout);
    return 0;
}

/**
 * @brief Prints a textual cache profiling report.
 *
 * @param pid Target process ID that was profiled.
 * @param interval_ms Sample interval in milliseconds.
 * @param sample_count Number of samples in p_stats_array.
 * @param p_stats_array Sample array produced by capture.
 */
void printmsg_logger_profile_report(pid_t pid, uint32_t interval_ms, uint32_t sample_count, const struct printmsg_cache_stats *p_stats_array) {
    if (sample_count == 0 || p_stats_array == NULL) {
        return;
    }

    printf("Cache profile for PID %d every %" PRIu32 " ms (%" PRIu32 " samples)\n", pid, interval_ms, sample_count);

    for (uint32_t i = 0; i < sample_count; ++i) {
        printf("Sample %" PRIu32 ":\n", i + 1U);
        printmsg_print_level("  L1", &p_stats_array[i].l1);
        printmsg_print_level("  L2", &p_stats_array[i].l2);
        printmsg_print_level("  LLC", &p_stats_array[i].llc);
    }
}

/**
 * @brief Captures and prints cache samples as they are gathered.
 *
 * @param pid Target process ID.
 * @param interval_ms Delay between samples in milliseconds. Must be > 0.
 * @param sample_count Number of samples to capture. Must be > 0.
 *
 * @return Status code.
 * @retval 0 Success.
 * @retval Negative errno code Failure while creating sampler, reading counters,
 *         waiting between samples, or from callback return.
 */
int printmsg_logger_profile_stream(pid_t pid, uint32_t interval_ms, uint32_t sample_count) {
    printf("Cache profile for PID %d every %" PRIu32 " ms (%" PRIu32 " samples)\n", pid, interval_ms, sample_count);
    return printmsg_profiler_iterate(pid, interval_ms, sample_count, printmsg_stream_sample_callback, NULL);
}
